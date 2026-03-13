
/*
 * File: hap-sat-hap.cc
 * Scenario: Two HAPs (Gateways) at 20 km <-> GEO Satellite <-> Users on Ground.
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

NS_LOG_COMPONENT_DEFINE("HapSatHap");

std::map<uint64_t, uint32_t> g_packetLastSender;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_hopStats;

std::string GetNodeName(uint32_t id, NodeContainer gwNodes, NodeContainer userNodes, NodeContainer satNodes)
{
    for (uint32_t i = 0; i < satNodes.GetN(); ++i) {
        if (id == satNodes.Get(i)->GetId()) return "SAT_" + std::to_string(i+1);
    }
    for (uint32_t i = 0; i < gwNodes.GetN(); ++i) {
        if (id == gwNodes.Get(i)->GetId()) return "HAP_" + std::to_string(i+1);
    }
    for (uint32_t i = 0; i < userNodes.GetN(); ++i) {
        if (id == userNodes.Get(i)->GetId()) return "USER_" + std::to_string(i+1);
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

void Ipv4TxTrace(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    uint32_t nodeId = GetNodeIdFromContext(context);
    g_packetLastSender[packet->GetUid()] = nodeId;
}

void Ipv4RxTrace(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
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

void ReceivePacket(Ptr<Socket> socket) { while (socket->Recv()) { } }

static void GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval)
{
    if (pktCount > 0) {
        socket->Send(Create<Packet>(pktSize));
        Simulator::Schedule(pktInterval, &GenerateTraffic, socket, pktSize, pktCount - 1, pktInterval);
    } else {
        socket->Close();
    }
}

int main(int argc, char* argv[])
{
    uint32_t packetSize = 1500;
    uint32_t numPackets = 1000;
    std::string intervalStr("265ms");
    double simLength = 10.; //300.0;
    double hapHeight = 20000.0;

    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packet (bytes)", packetSize);
    cmd.AddValue("numPackets", "Number of packets", numPackets);
    cmd.AddValue("interval", "Interval between packets", intervalStr);
    cmd.Parse(argc, argv);

    Time interPacketInterval = Time(intervalStr);

    // === SATELLITE SETUP ===
    std::string customDataPath = "/home/igor/workspace_ns3.43/ns-3.43/contrib/sibgu-hap/examples";
    
    // ПРАВИЛЬНЫЙ способ: переменная окружения
    setenv("NS3_SAT_SCENARIO_PATH", customDataPath.c_str(), 1);
    
    // Только эти атрибуты поддерживаются через Config
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));
    
    // Отключаем модели ошибок
    SatPhyRxCarrierConf::ErrorModel em(SatPhyRxCarrierConf::EM_NONE);
    Config::SetDefault("ns3::SatUtHelper::FwdLinkErrorModel", EnumValue(em));
    Config::SetDefault("ns3::SatGwHelper::RtnLinkErrorModel", EnumValue(em));

    // Создаём SimulationHelper ПОСЛЕ настройки
    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("hap-sat-hap");
    simulationHelper->SetSimulationTime(simLength);
    simulationHelper->LoadScenario("geo-33E");
    //simulationHelper->LoadScenario("geo-33E-lora");
    simulationHelper->CreateSatScenario(SatHelper::FULL);

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    // === MOBILITY ===
    NodeContainer gwNodes = topology->GetGwNodes();
    NodeContainer gwUserNodes = topology->GetGwUserNodes();
    NodeContainer satNodes;

    NS_LOG_UNCOND("--- Positioning Nodes ---");
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, hapHeight));
    positionAlloc->Add(Vector(1.0, 0.0, hapHeight));
    positionAlloc->Add(Vector(0.0, 0.05, 0.0));
    positionAlloc->Add(Vector(1.0, 0.05, 0.0));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(gwNodes);
    mobility.Install(gwUserNodes);

    // === TRACING ===
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&Ipv4TxTrace));
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&Ipv4RxTrace));

    // === TRAFFIC ===
    Ptr<Node> sourceNode = gwUserNodes.Get(0);
    Ptr<Node> sinkNode = gwUserNodes.Get(1);
    Ipv4Address sinkAddr = sinkNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

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

    // === RUN ===
    NS_LOG_UNCOND("\n=== Starting Simulation ===");
    Simulator::ScheduleWithContext(source->GetNode()->GetId(), Seconds(1.0),
        &GenerateTraffic, source, packetSize, numPackets, interPacketInterval);
    Simulator::Stop(Seconds(simLength));
    Simulator::Run();

    // === OUTPUT ===
    std::cout << "\n=== Network Map ===" << std::endl;
    std::cout << std::left << std::setw(10) << "NodeID"
              << std::left << std::setw(15) << "Name"
              << std::left << std::setw(18) << "IP Address" << std::endl;
    std::cout << std::string(43, '-') << std::endl;

    NodeContainer allNodes = NodeContainer::GetGlobal();
    for (uint32_t i = 0; i < allNodes.GetN(); ++i) {
        Ptr<Node> node = allNodes.Get(i);
        std::string name = GetNodeName(node->GetId(), gwNodes, gwUserNodes, satNodes);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (ipv4) {
            uint32_t nIf = ipv4->GetNInterfaces();
            for (uint32_t j = 1; j < nIf; ++j) {
                Ipv4Address addr = ipv4->GetAddress(j, 0).GetLocal();
                std::cout << std::left << std::setw(10) << node->GetId()
                          << std::left << std::setw(15) << name
                          << std::left << std::setw(18) << IpToString(addr) << std::endl;
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
        std::string srcName = GetNodeName(key.first, gwNodes, gwUserNodes, satNodes);
        std::string dstName = GetNodeName(key.second, gwNodes, gwUserNodes, satNodes);
        std::cout << std::left << std::setw(30) << (srcName + " -> " + dstName)
                  << std::right << std::setw(15) << count << std::endl;
    }
    std::cout << std::string(45, '-') << std::endl;

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

    monitor->SerializeToXmlFile("hap-sat-hap-stats.xml", true, true);
    std::cout << "\n=== End of Simulation ===" << std::endl;

    Simulator::Destroy();
    return 0;
}