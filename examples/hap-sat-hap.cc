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
#include "ns3/system-path.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("HapSatHap");

std::map<uint64_t, uint32_t> g_packetLastSender;
std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_hopStats;

std::string GetNodeName(uint32_t id, NodeContainer gwNodes,
     NodeContainer userNodes, NodeContainer satNodes, NodeContainer utNodes)
{
    // 1. Стандартные проверки по спискам
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

    // 2. Интеллектуальная проверка узла
    Ptr<Node> node = NodeList::GetNode(id);
    if (node != nullptr)
    {
        for (uint32_t i = 0; i < node->GetNDevices(); ++i)
        {
            Ptr<NetDevice> dev = node->GetDevice(i);
            Ptr<Channel> ch = dev->GetChannel();
            if (ch)
            {
                // --- ПРОВЕРКА ПО ИМЕНИ КАНАЛА ---
                std::string channelName = ch->GetInstanceTypeId().GetName();
                
                // Если это стандартный спутник (содержит "Satellite")
                if (channelName.find("Satellite") != std::string::npos)
                {
                    return "SAT_CHAN_" + std::to_string(id);
                }
                
                // Если это упрощенная модель спутника (содержит "SatSimple")
                if (channelName.find("SatSimple") != std::string::npos)
                {
                    return "SAT_CHAN_" + std::to_string(id);
                }
            }
        }
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


int MakeLinkToScenario(std::string myScenarioName)
{
// === ИСПРАВЛЕНИЕ ПУТЕЙ (Абсолютные пути) ===
    
    // 1. Получаем корневую директорию проекта 
    // (она же Current Working Directory при запуске через ./ns3)
    char buffer[1024];
    std::string ns3Root;
    if (getcwd(buffer, sizeof(buffer)) != nullptr) {
        ns3Root = buffer;
    } else {
        std::cerr << "Error getting current directory" << std::endl;
        return 1;
    }
    std::cout << "NS-3 Root detected: " << ns3Root << std::endl;

    // 2. Формируем АБСОЛЮТНЫЙ путь к НАШЕЙ папке со сценой (источник)
    // Путь: [ROOT]/contrib/sibgu-hap/data/scenarios/geo-33E-hap
    std::string sourceScenarioPath = SystemPath::Append(ns3Root, 
        "contrib/sibgu-hap/data/scenarios/" + myScenarioName);

    // Проверяем, существует ли эта папка
    struct stat info;
    if (stat(sourceScenarioPath.c_str(), &info) != 0) {
        std::cerr << "ERROR: Your custom scenario not found at: " 
        << sourceScenarioPath << std::endl;
        return 1;
    }
    std::cout << "Custom scenario found: " << sourceScenarioPath << std::endl;

    // 3. Формируем АБСОЛЮТНЫЙ путь к папке, куда СМОТРИТ модуль (целевая папка)
    // В которую поместим ссылку на папку с нашей сценой.
    // Путь: [ROOT]/contrib/satellite/data/scenarios
    std::string systemScenarioDir = SystemPath::Append(ns3Root, 
        "contrib/satellite/data/scenarios");

    // Проверяем, существует ли стандартная папка (должна быть в любой нормальной установке)
    if (stat(systemScenarioDir.c_str(), &info) != 0) {
        std::cerr << "ERROR: Standard satellite scenario folder not found at: " 
        << systemScenarioDir << std::endl;
        std::cerr << "Check your NS-3 installation." << std::endl;
        return 1;
    }
    std::cout << "System scenario folder found: " << systemScenarioDir << std::endl;

    // 4. Формируем полный путь к будущей ссылке
    std::string linkPath = SystemPath::Append(systemScenarioDir, myScenarioName);

    // 5. Удаляем старую ссылку, если она осталась с прошлого запуска
    unlink(linkPath.c_str());

    // 6. Создаем символическую ссылку
    // Создаем файл-ссылку внутри стандартной папки, указывающий на нашу папку со сценой
    std::cout << "Creating symlink: " << linkPath << " -> " << sourceScenarioPath << std::endl;
    if (symlink(sourceScenarioPath.c_str(), linkPath.c_str()) != 0) {
        std::cerr << "FATAL: Failed to create symlink." << std::endl;
        perror("symlink"); 
        return 1;
    }
    std::cout << "Symlink created successfully!" << std::endl;
    return 0;
}

int main(int argc, char* argv[])
{
    uint32_t packetSize = 1500;
    uint32_t numPackets = 1000;
    std::string intervalStr("265ms");
    double simLength = 10.; //300.0;
   // double hapHeight = 20000.0;

    CommandLine cmd;
    cmd.AddValue("packetSize", "Size of packet (bytes)", packetSize);
    cmd.AddValue("numPackets", "Number of packets", numPackets);
    cmd.AddValue("interval", "Interval between packets", intervalStr);
    cmd.Parse(argc, argv);

    Time interPacketInterval = Time(intervalStr);

    std::string myScenarioName = "geo-33E-hap";
    if (MakeLinkToScenario(myScenarioName)) return 1;

    // === SATELLITE SETUP ===
    
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));
    SatPhyRxCarrierConf::ErrorModel em(SatPhyRxCarrierConf::EM_NONE);
    Config::SetDefault("ns3::SatUtHelper::FwdLinkErrorModel", EnumValue(em));
    Config::SetDefault("ns3::SatGwHelper::RtnLinkErrorModel", EnumValue(em));

    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("hap-sat-hap");
    simulationHelper->SetSimulationTime(simLength);

    // 7. Загружаем сценарий
    simulationHelper->LoadScenario(myScenarioName);
    
    simulationHelper->CreateSatScenario(SatHelper::FULL);   

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    // === MOBILITY ===

    // === TRACING ===
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&Ipv4TxTrace));
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&Ipv4RxTrace));
    
    // === TRAFFIC ===
    
    NodeContainer utNodes = topology->GetUtNodes();
    NodeContainer gwNodes = topology->GetGwNodes();
    NodeContainer gwUserNodes = topology->GetGwUserNodes();
    NodeContainer satNodes = topology->GetOrbiterNodes(); 
    
// --- ОТЛАДКА: Проверим, какие узлы считаются спутниками ---
    std::cout << "DEBUG: Found " << satNodes.GetN() << " Orbiters (Satellites)." << std::endl;
    for (uint32_t i = 0; i < satNodes.GetN(); ++i) {
        std::cout << "  Orbiter ID: " << satNodes.Get(i)->GetId() << std::endl;
    }
    // -------------------------------------------------------    



    Ptr<Node> sourceNode;
    Ptr<Node> sinkNode;

    if (utNodes.GetN() < 2) {
        // Fallback если UT нет
        std::cout << "WARNING: Not enough UT nodes. Using GW User nodes." << std::endl;
        sourceNode = gwUserNodes.Get(0);
        sinkNode = gwUserNodes.Get(gwUserNodes.GetN() - 1);
    } 
    else 
    {
        std::cout << "Analyzing UT to GW mapping..." << std::endl;
        
        // 1. Создаем мап: IP адрес шлюза -> Сам узел шлюза
        std::map<Ipv4Address, Ptr<Node>> gwIpToNodeMap;
        
        for (uint32_t i = 0; i < gwNodes.GetN (); ++i)
        {
            Ptr<Node> gw = gwNodes.Get (i);
            Ptr<Ipv4> ipv4 = gw->GetObject<Ipv4> ();
            for (uint32_t j = 1; j < ipv4->GetNInterfaces (); ++j)
            {
                // Собираем все IP адреса всех интерфейсов шлюза
                Ipv4Address addr = ipv4->GetAddress (j, 0).GetLocal ();
                if (addr != Ipv4Address ()) {
                    gwIpToNodeMap[addr] = gw;
                }
            }
        }

        // 2. Группируем UT по шлюзам, к которым они подключены (через Default Route)
        std::map<Ptr<Node>, std::vector<Ptr<Node>>> gwToUtsMap;
        Ipv4StaticRoutingHelper routingHelper;

        for (uint32_t i = 0; i < utNodes.GetN (); ++i)
        {
            Ptr<Node> ut = utNodes.Get (i);
            Ptr<Ipv4> ipv4 = ut->GetObject<Ipv4> ();
            
            // Получаем таблицу статической маршрутизации для UT
            Ptr<Ipv4StaticRouting> staticRouting = routingHelper.GetStaticRouting (ipv4);
            
            if (staticRouting)
            {
                uint32_t numRoutes = staticRouting->GetNRoutes ();
                for (uint32_t j = 0; j < numRoutes; ++j)
                {
                    Ipv4RoutingTableEntry route = staticRouting->GetRoute (j);
                    
                    // Используем GetZero() для получения 0.0.0.0
                    if (route.GetDest () == Ipv4Address::GetZero () && 
                        route.GetDestNetworkMask () == Ipv4Mask::GetZero ())
                    {
                        Ipv4Address gwIp = route.GetGateway ();
                        
                        if (gwIpToNodeMap.count (gwIp) > 0) {
                            Ptr<Node> associatedGw = gwIpToNodeMap[gwIp];
                            gwToUtsMap[associatedGw].push_back (ut);
                        }
                        break; 
                    }
                }
            }
        }

        // 3. Выбираем источника и приемника из РАЗНЫХ групп
        if (gwToUtsMap.size () >= 2)
        {
            auto it = gwToUtsMap.begin ();
            std::vector<Ptr<Node>> group1 = it->second; // Группа 1-го шлюза
            it++;
            std::vector<Ptr<Node>> group2 = it->second; // Группа 2-го шлюза

            sourceNode = group1[0];
            sinkNode = group2[0]; 

            std::cout << "Selected Source: UT Node " << sourceNode->GetId() 
                      << " (via GW Node " << gwToUtsMap.begin ()->first->GetId()
                      << ")" << std::endl;
            std::cout << "Selected Sink:   UT Node " << sinkNode->GetId() 
                      << " (via GW Node " << it->first->GetId() << ")" << std::endl;
        }
        else
        {
            std::cout << "WARNING: All UTs seem to be behind the SAME GW or routing is complex. Falling back to first/last." << std::endl;
            sourceNode = utNodes.Get(0);
            sinkNode = utNodes.Get(utNodes.GetN() - 1);
        }
    }

    // Вывод IP для отладки
    Ipv4Address sinkAddr = sinkNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

    std::cout << "Source Node ID: " << sourceNode->GetId() 
              << ", Sink Node ID: " << sinkNode->GetId() 
              << ", Sink IP: " << sinkAddr << std::endl;

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

    // Берем только нужные нам узлы, игнорируя сотни лишних из файла сценария
    NodeContainer nodesToPrint;
    nodesToPrint.Add(gwNodes);
    nodesToPrint.Add(gwUserNodes);

    // Чтобы напечатать все узлы раскомиентировать это.
    //nodesToPrint = NodeContainer::GetGlobal();
    for (uint32_t i = 0; i < nodesToPrint.GetN(); ++i) {
        Ptr<Node> node = nodesToPrint.Get(i);
        std::string name = GetNodeName(node->GetId(), gwNodes, gwUserNodes, satNodes, utNodes);
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
        std::string srcName = GetNodeName(key.first, gwNodes, gwUserNodes, satNodes, utNodes);
        std::string dstName = GetNodeName(key.second, gwNodes, gwUserNodes, satNodes, utNodes);
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
