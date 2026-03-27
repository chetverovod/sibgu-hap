/*
 * File: hap-constellation-hap.cc
 * Scenario: Fast Handover in Iridium LEO Constellation.
 *
 * FEATURES:
 * 1. Uses the correct scenario folder: "constellation-iridium-next-66-sats".
 * 2. OPTIMIZATION: Uses SetBeamSet to limit topology to 2 beams.
 *    This reduces simulation time from HOURS to MINUTES.
 * 3. RELIABILITY: Uses UT-to-UT traffic to ensure packets are delivered
 *    and routing works during handover.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/config-store-module.h"
#include "ns3/mobility-module.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("HapSatConstellationHandover");

std::map<uint64_t, uint32_t> g_packetLastSender;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_hopStats;

std::string GetNodeName(uint32_t id, NodeContainer gwNodes,
     NodeContainer userNodes, NodeContainer satNodes, NodeContainer utNodes)
{
    for (uint32_t i = 0; i < satNodes.GetN(); ++i) {
        if (id == satNodes.Get(i)->GetId()) return "SAT_" + std::to_string(i+1);
    }
    for (uint32_t i = 0; i < utNodes.GetN(); ++i) {
        if (id == utNodes.Get(i)->GetId()) return "UT_" + std::to_string(i+1);
    }
    for (uint32_t i = 0; i < gwNodes.GetN(); ++i) {
        if (id == gwNodes.Get(i)->GetId()) return "GW_" + std::to_string(i+1);
    }
    for (uint32_t i = 0; i < userNodes.GetN(); ++i) {
        if (id == userNodes.Get(i)->GetId()) return "GW_USER_" + std::to_string(i+1);
    }
    return "Node_" + std::to_string(id);
}

uint32_t GetNodeIdFromContext(std::string context)
{
    std::string prefix = "/NodeList/";
    size_t pos = context.find(prefix);
    if (pos == std::string::npos) return 9999;
    std::string sub = context.substr(pos + prefix.length());
    size_t slashPos = sub.find("/");
    return std::stoi(sub.substr(0, slashPos));
}

std::string IpToString(Ipv4Address addr)
{
    std::ostringstream oss;
    oss << addr;
    return oss.str();
}

void Ipv4TxTrace(std::string context, Ptr<const Packet> packet, 
    Ptr<Ipv4> ipv4, uint32_t interface)
{
    uint32_t nodeId = GetNodeIdFromContext(context);
    g_packetLastSender[packet->GetUid()] = nodeId;
}

void Ipv4RxTrace(std::string context, Ptr<const Packet> packet,
     Ptr<Ipv4> ipv4, uint32_t interface)
{
    uint32_t nodeId = GetNodeIdFromContext(context);
    uint64_t uid = packet->GetUid();
    if (g_packetLastSender.find(uid) != g_packetLastSender.end()) {
        uint32_t senderId = g_packetLastSender[uid];
        if (senderId != nodeId) {
            g_hopStats[std::make_pair(senderId, nodeId)]++;
        }
    }
}

void ReceivePacket(Ptr<Socket> socket)
{ 
    while (socket->Recv()) { }
}

static void GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize,
     uint32_t pktCount, Time pktInterval)
{
    if (pktCount > 0) {
        socket->Send(Create<Packet>(pktSize));
        Simulator::Schedule(pktInterval, &GenerateTraffic, socket,
            pktSize, pktCount - 1, pktInterval);
    } else {
        socket->Close();
    }
}

int main(int argc, char* argv[])
{
    uint32_t packetSize = 1500;
    uint32_t numPackets = 5000;
    std::string intervalStr("100ms");
    //double simLength = 600.0; // 10 minutes - sufficient for LEO movement
    double simLength = 300.0; // 10 minutes - sufficient for LEO movement
    // CORRECT SCENARIO NAME from your working example
    std::string scenarioFolder = "constellation-iridium-next-66-sats";
   
    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packet (bytes)", packetSize);
    cmd.AddValue("numPackets", "Number of packets", numPackets);
    cmd.AddValue("interval", "Interval between packets", intervalStr);
    cmd.AddValue("scenarioFolder", "Scenario folder name", scenarioFolder);
    cmd.Parse(argc, argv);

    Time interPacketInterval = Time(intervalStr);

    // === CONFIGURATION ===
    
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));

    Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                       EnumValue(SatEnums::REGENERATION_NETWORK));
    
    Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
    Config::SetDefault("ns3::SatOrbiterUserPhy::QueueSize", UintegerValue(100000));

    Config::SetDefault("ns3::PointToPointIslHelper::IslDataRate",
                       DataRateValue(DataRate("100Mb/s")));
    
    Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionEachRequest", BooleanValue(false));
    Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionPeriod", TimeValue(Seconds(10)));

    SatPhyRxCarrierConf::ErrorModel em(SatPhyRxCarrierConf::EM_NONE);
    Config::SetDefault("ns3::SatUtHelper::FwdLinkErrorModel", EnumValue(em));
    Config::SetDefault("ns3::SatGwHelper::RtnLinkErrorModel", EnumValue(em));

    // === SIMULATION SETUP ===

    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("hap-sat-handover");
    simulationHelper->SetSimulationTime(simLength);
    simulationHelper->LoadScenario(scenarioFolder);
    
    // OPTIMIZATION: Limit to 2 specific beams.
    // Without this, NS-3 loads ALL beams (hundreds), causing hours of simulation time.
    // Beam 1 and Beam 24 are likely far apart, ensuring different satellites are used.
    //std::set<uint32_t> beams = {1, 24}; 
    std::set<uint32_t> beams = {1, 72}; 
    simulationHelper->SetBeamSet(beams);
    simulationHelper->SetUserCountPerUt(1); 

    simulationHelper->CreateSatScenario(SatHelper::FULL);   

  // === TRACING ===
    
    // 1. Сначала получаем доступ к топологии и спутникам
    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
    NodeContainer satNodes = topology->GetOrbiterNodes(); 
    
    // 2. Теперь переменная satNodes видна здесь, и мы можем использовать её в цикле
    for (uint32_t i = 0; i < satNodes.GetN (); ++i)
    {
        Ptr<Node> sat = satNodes.Get (i);
        Ptr<Ipv4> ipv4 = sat->GetObject<Ipv4> ();
        if (ipv4)
        {
            // Используем TraceConnect, потому что ваш колбэк Ipv4TxTrace принимает "std::string context"
            // Если использовать TraceConnectWithoutContext, будет ошибка компиляции несоответствия типов
            ipv4->TraceConnect ("Tx", "SatTx", MakeCallback (&Ipv4TxTrace));
            ipv4->TraceConnect ("Rx", "SatRx", MakeCallback (&Ipv4RxTrace));
        }
    }
    // === TRAFFIC SETUP ===
/*    
    NodeContainer utNodes = topology->GetUtNodes();
    NodeContainer gwNodes = topology->GetGwNodes();
    NodeContainer gwUserNodes = topology->GetGwUserNodes();
    NodeContainer satNodes = topology->GetOrbiterNodes(); 
    
    std::cout << "Found " << satNodes.GetN() << " Satellites." << std::endl;
    std::cout << "Found " << utNodes.GetN() << " UTs (limited by BeamSet)." << std::endl;

    Ptr<Node> sourceNode;
    Ptr<Node> sinkNode;

    // SELECTION STRATEGY: UT to UT.
    // We select UT_1 and UT_2. They are in the same subnet (10.x.x.x),
    // which makes routing much more reliable than UT->GW_User in complex topologies.
    if (utNodes.GetN() >= 2)
    {
        sourceNode = utNodes.Get(0);
        sinkNode = utNodes.Get(1);
        
        std::cout << "Selected Source: UT Node " << sourceNode->GetId() << std::endl;
        std::cout << "Selected Sink:   UT Node " << sinkNode->GetId() << std::endl;
        std::cout << "Simulation will run for " << simLength << " seconds to observe handover." << std::endl;
    }
    else
    {
        std::cerr << "Error: Need at least 2 UTs. Found: " << utNodes.GetN() << std::endl;
        std::cerr << "Try changing BeamSet in the code." << std::endl;
        return 1;
    }

    // UTs usually have IP on interface 1 (e.g. 10.1.0.1)
    Ipv4Address sinkAddr = sinkNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

    std::cout << "Sink IP: " << sinkAddr << std::endl;
    */


       // === TRAFFIC SETUP ===
    NodeContainer utNodes = topology->GetUtNodes();
    NodeContainer gwNodes = topology->GetGwNodes();
    NodeContainer gwUserNodes = topology->GetGwUserNodes(); // Убедитесь, что этот контейнер есть
    //NodeContainer satNodes = topology->GetOrbiterNodes(); 
    
    std::cout << "Found " << satNodes.GetN() << " Satellites." << std::endl;
    std::cout << "Found " << utNodes.GetN() << " UTs (limited by BeamSet)." << std::endl;
    std::cout << "Found " << gwUserNodes.GetN() << " GW Users." << std::endl;

    Ptr<Node> sourceNode;
    Ptr<Node> sinkNode;

    // ИСПРАВЛЕНИЕ: Используем UT как источник, а GW User как приемник.
    // Это гарантирует, что маршрут существует (UT -> Sat -> GW -> GW User).
    if (utNodes.GetN() >= 1 && gwUserNodes.GetN() >= 1)
    {
        sourceNode = utNodes.Get(0); // Источник: UT_1
        sinkNode = gwUserNodes.Get(0); // Приемник: GW User (пользователь шлюза)
        
        std::cout << "Selected Source: UT Node " << sourceNode->GetId() << std::endl;
        std::cout << "Selected Sink:   GW User Node " << sinkNode->GetId() << std::endl;
        std::cout << "Simulation will run for " << simLength << " seconds to observe handover." << std::endl;
    }
    else
    {
        std::cerr << "Error: Need at least 1 UT and 1 GW User." << std::endl;
        return 1;
    }

    // Получаем IP адрес приемника. Обычно это интерфейс 1 (после loopback)
    Ipv4Address sinkAddr = sinkNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

    std::cout << "Sink IP: " << sinkAddr << std::endl;


    // Setup Socket
    uint16_t port = 9;
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(sinkNode, tid);
    recvSink->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));
    
    Ptr<Socket> source = Socket::CreateSocket(sourceNode, tid);
    source->Connect(InetSocketAddress(sinkAddr, port));

    // === FLOW MONITOR ===
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // === RUN SIMULATION ===
    NS_LOG_UNCOND("\n=== Starting Simulation ===");
    Simulator::ScheduleWithContext(source->GetNode()->GetId(), Seconds(1.0),
        &GenerateTraffic, source, packetSize, numPackets, interPacketInterval);
    Simulator::Stop(Seconds(simLength));
    Simulator::Run();

    // === OUTPUT STATISTICS ===
    std::cout << "\n=== Network Map ===" << std::endl;
    std::cout << std::left << std::setw(10) << "NodeID"
              << std::left << std::setw(15) << "Name"
              << std::left << std::setw(18) << "IP Address" << std::endl;
    std::cout << std::string(43, '-') << std::endl;

    NodeContainer nodesToPrint;
    nodesToPrint.Add(sourceNode);
    nodesToPrint.Add(sinkNode);
    nodesToPrint.Add(satNodes);

    for (uint32_t i = 0; i < nodesToPrint.GetN(); ++i) {
        Ptr<Node> node = nodesToPrint.Get(i);
        std::string name = GetNodeName(node->GetId(), gwNodes, gwUserNodes, satNodes, utNodes);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (ipv4) {
            uint32_t nIf = ipv4->GetNInterfaces();
            for (uint32_t j = 1; j < nIf; ++j) {
                Ipv4Address addr = ipv4->GetAddress(j, 0).GetLocal();
                if (addr != Ipv4Address()) {
                     std::cout << std::left << std::setw(10) << node->GetId()
                              << std::left << std::setw(15) << name
                              << std::left << std::setw(18) << IpToString(addr) << std::endl;
                }
            }
        }
    }
    std::cout << std::string(43, '-') << std::endl;

    std::cout << "\n=== Intermediate Hop Statistics ===" << std::endl;
    std::cout << std::left << std::setw(30) << "Link (Src -> Dst)"
              << std::right << std::setw(15) << "Packets" << std::endl;
    std::cout << std::string(45, '-') << std::endl;

    std::vector<std::pair<uint32_t, uint32_t>> sortedHops;
    for (auto const& kv : g_hopStats) { sortedHops.push_back(kv.first); }
    std::sort(sortedHops.begin(), sortedHops.end());

    for (auto const& key : sortedHops) {
        uint32_t count = g_hopStats[key];
        std::string srcName = GetNodeName(key.first, gwNodes, gwUserNodes, satNodes, utNodes);
        std::string dstName = GetNodeName(key.second, gwNodes, gwUserNodes, satNodes, utNodes);
        std::cout << std::left << std::setw(30) << (srcName + " -> " + dstName)
                  << std::right << std::setw(15) << count << std::endl;
    }
    std::cout << std::string(45, '-') << std::endl;
    std::cout << "If you see UT_1 sending to SAT_X and later SAT_Y, handover is working!" << std::endl;

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n=== End-to-End Results ===" << std::endl;
    std::cout << std::left << std::setw(5) << "ID"
              << std::left << std::setw(18) << "Src IP"
              << std::left << std::setw(18) << "Dst IP"
              << std::right << std::setw(8) << "Tx"
              << std::right << std::setw(8) << "Rx"
              << std::right << std::setw(10) << "Loss%"
              << std::right << std::setw(12) << "Kbps"
              << std::right << std::setw(10) << "Delay"
              << std::right << std::setw(10) << "Jitter" << std::endl;
    std::cout << std::string(95, '-') << std::endl;

    for (auto const& [flowId, flowStats] : stats) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);
        double lossRatio = 0.0;
        if (flowStats.txPackets > 0) {
            lossRatio = ((double)(flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets) * 100.0;
        }

        double throughput = 0.0;
        double delay = 0.0;
        double jitter = 0.0;

        if (flowStats.rxPackets > 0) {
            throughput = (flowStats.rxBytes * 8.0) / (flowStats.timeLastRxPacket.GetSeconds() - flowStats.timeFirstTxPacket.GetSeconds());
            delay = flowStats.delaySum.GetSeconds() / flowStats.rxPackets;
            if (flowStats.rxPackets > 1) {
                jitter = flowStats.jitterSum.GetSeconds() / (flowStats.rxPackets - 1);
            }
            std::cout << std::left << std::setw(5) << flowId
                      << std::left << std::setw(18) << IpToString(t.sourceAddress)
                      << std::left << std::setw(18) << IpToString(t.destinationAddress)
                      << std::right << std::setw(8) << flowStats.txPackets
                      << std::right << std::setw(8) << flowStats.rxPackets
                      << std::right << std::setw(9) << std::fixed << std::setprecision(1) << lossRatio << "%"
                      << std::right << std::setw(12) << std::fixed << std::setprecision(1) << (throughput / 1000.0)
                      << std::right << std::setw(9) << std::fixed << std::setprecision(0) << (delay * 1000.0) << "ms"
                      << std::right << std::setw(9) << std::fixed << std::setprecision(0) << (jitter * 1000.0) << "ms" << std::endl;
        } else {
            std::cout << std::left << std::setw(5) << flowId
                      << std::left << std::setw(18) << t.sourceAddress
                      << std::left << std::setw(18) << t.destinationAddress
                      << std::right << std::setw(8) << flowStats.txPackets
                      << std::right << std::setw(8) << 0
                      << std::right << std::setw(10) << "100.0%"
                      << std::right << std::setw(12) << "-"
                      << std::right << std::setw(10) << "-"
                      << std::right << std::setw(10) << "-" << std::endl;
        }
    }
    std::cout << std::string(95, '-') << std::endl;

    monitor->SerializeToXmlFile("hap-handover-stats.xml", true, true);
    std::cout << "\n=== End of Simulation ===" << std::endl;

    Simulator::Destroy();
    return 0;
}