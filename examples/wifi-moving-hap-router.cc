/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// Extended script based on ns-3.43/examples/wireless/wifi-simple-adhoc.cc
// Scenario: HAP (High Altitude Platform) with dual interfaces acts as a router.
// Network A: 2.4GHz (802.11b) connecting HAP and Ground Terminal A.
// Network B: 5GHz  (802.11a) connecting HAP and Ground Terminal B.
// Traffic flows from Ground A -> HAP -> Ground B.
// MODIFICATION: HAP moves in a circle and uses directional antennas to track ground.

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/cosine-antenna-model.h"
#include "ns3/isotropic-antenna-model.h" // Добавлен заголовок для изотропной антенны
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiHapDualBandMoving");

// --- Глобальные переменные для доступа из callback-функции ---
Ptr<ConstantVelocityMobilityModel> g_hapMobility;
Ptr<CosineAntennaModel> g_hapAntennaA;
Ptr<CosineAntennaModel> g_hapAntennaB;

// Параметры кругового движения
double g_circleRadius = 6000.0; // Радиус круга (м)
double g_angularVelocity = 2 * M_PI / 100.0; // Полный оборот за 100 секунд (рад/с)

void ReceivePacket(Ptr<Socket> socket)
{
    while (socket->Recv())
    {
        // NS_LOG_UNCOND("Received one packet!");
    }
}

static void GenerateTraffic(Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval)
{
    if (pktCount > 0)
    {
        socket->Send(Create<Packet>(pktSize));
        Simulator::Schedule(pktInterval, &GenerateTraffic, socket, pktSize, pktCount - 1, pktInterval);
    }
    else
    {
        socket->Close();
    }
}

// --- Функция обновления позиции HAP и навигации антенны ---
void UpdateHapState()
{
    if (!g_hapMobility)
    {
        return;
    }

    // 1. Обновление вектора скорости для движения по кругу
    Vector currentPos = g_hapMobility->GetPosition();
    double x = currentPos.x;
    double y = currentPos.y;

    // Вычисляем касательный вектор скорости (перпендикулярно радиусу)
    // Для движения против часовой стрелки: Vx = -omega * y, Vy = omega * x
    double vx = -g_angularVelocity * y;
    double vy = g_angularVelocity * x;

    g_hapMobility->SetVelocity(Vector(vx, vy, 0.0));

    // 2. Навигация антенны (Beam Steering)
    // Мы хотим направить антенну HAP в центр круга (0,0,0).
    // Вычисляем угол (азимут) от HAP к центру.
    double angleRad = std::atan2(-y, -x);
    double angleDeg = angleRad * 180.0 / M_PI;

    NS_LOG_DEBUG("HAP Update: Pos=(" << x << "," << y << "), Angle=" << angleDeg << " deg");

    // Устанавливаем ориентацию антенн через систему атрибутов
    if (g_hapAntennaA)
    {
        g_hapAntennaA->SetAttribute("Orientation", DoubleValue(angleDeg));
    }
    if (g_hapAntennaB)
    {
        g_hapAntennaB->SetAttribute("Orientation", DoubleValue(angleDeg));
    }

    // Планируем следующий вызов функции через 0.1 секунды
    Simulator::Schedule(Seconds(0.1), &UpdateHapState);
}

int main(int argc, char* argv[])
{
    // --- General Parameters ---
    std::string phyModeA("DsssRate1Mbps"); // 802.11b
    std::string phyModeB("OfdmRate6Mbps"); // 802.11a
    uint32_t packetSize{1000}; // bytes
    uint32_t numPackets{10};
    Time interPacketInterval{"40ms"};
    bool verbose{false};

    // HAP Parameters
    double hight{20000.0};    // meters
    double Pdbm{20.};       // Transmitter power (dBm)
    double antGain{20.};    // Antenna gain (dB)

    // Ground separation (distance between terminal A and B on the ground)
    double groundDistance{5000.0}; 

    CommandLine cmd(__FILE__);
    cmd.AddValue("phyModeA", "Wifi Phy mode Network A (2.4GHz)", phyModeA);
    cmd.AddValue("phyModeB", "Wifi Phy mode Network B (5GHz)", phyModeB);
    cmd.AddValue("packetSize", "size of application packet sent", packetSize);
    cmd.AddValue("numPackets", "number of packets generated", numPackets);
    cmd.AddValue("interval", "interval between packets", interPacketInterval);
    cmd.AddValue("verbose", "turn on all WifiNetDevice log components", verbose);
    cmd.AddValue("hight", "HAP height (m)", hight);
    cmd.AddValue("txPower", "Power of transmitter, (dBm)", Pdbm);
    cmd.AddValue("antGain", "Directional Antenna max gain (dBi)", antGain);
    cmd.AddValue("groundDistance", "Distance between ground terminals A and B (m)", groundDistance);
    cmd.Parse(argc, argv);

    // Create Nodes
    NodeContainer nodes;
    nodes.Create(3);

    // --- Network A Setup (2.4 GHz, 802.11b) ---
    WifiHelper wifiA;
    if (verbose)
        WifiHelper::EnableLogComponents();
    wifiA.SetStandard(WIFI_STANDARD_80211b);

    YansWifiPhyHelper wifiPhyA;
    // Примечание: TxGain/RxGain теперь управляются моделью антенны,
    // но базовые настройки мощности TxPowerStay/End остаются в Phy.
    wifiPhyA.Set("TxPowerStart", DoubleValue(Pdbm));
    wifiPhyA.Set("TxPowerEnd", DoubleValue(Pdbm));
    wifiPhyA.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannelA;
    wifiChannelA.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannelA.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(40.0)
                                   );
    wifiChannelA.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                   "m0", DoubleValue(1.0), 
                                   "m1", DoubleValue(1.0),
                                   "m2", DoubleValue(1.0));
    
    wifiPhyA.SetChannel(wifiChannelA.Create());

    WifiMacHelper wifiMacA;
    wifiA.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue(phyModeA),
                                  "ControlMode", StringValue(phyModeA));
    wifiMacA.SetType("ns3::AdhocWifiMac");
    
    // Install Net A на всех узлах (HAP и Ground A)
    NetDeviceContainer devicesA;
    devicesA.Add(wifiA.Install(wifiPhyA, wifiMacA, nodes.Get(0))); // HAP
    devicesA.Add(wifiA.Install(wifiPhyA, wifiMacA, nodes.Get(1))); // Ground A

    // --- НАСТРОЙКА АНТЕНН ДЛЯ NETWORK A ---
    
    // 1. Настраиваем HAP (Node 0) - Направленная антенна
    Ptr<WifiNetDevice> devA_Hap = DynamicCast<WifiNetDevice> (devicesA.Get(0));
    Ptr<CosineAntennaModel> antennaA_Hap = CreateObject<CosineAntennaModel>();
    antennaA_Hap->SetAttribute("Beamwidth", DoubleValue(60.0));
    antennaA_Hap->SetAttribute("MaxGain", DoubleValue(antGain));
    antennaA_Hap->SetAttribute("Orientation", DoubleValue(180.0)); // Начальная ориентация на запад
    // Устанавливаем антенну в Phy через атрибут
    devA_Hap->GetPhy()->SetAttribute("Antenna", PointerValue(antennaA_Hap));
    // Сохраняем указатель для вращения
    g_hapAntennaA = antennaA_Hap;

    // 2. Настраиваем Ground A (Node 1) - Изотропная антенна
    Ptr<WifiNetDevice> devA_Ground = DynamicCast<WifiNetDevice> (devicesA.Get(1));
    Ptr<IsotropicAntennaModel> antennaA_Ground = CreateObject<IsotropicAntennaModel>();
    antennaA_Ground->SetAttribute("Gain", DoubleValue(0.0)); // 0 dBi
    devA_Ground->GetPhy()->SetAttribute("Antenna", PointerValue(antennaA_Ground));


    // --- Network B Setup (5 GHz, 802.11a) ---
    WifiHelper wifiB;
    wifiB.SetStandard(WIFI_STANDARD_80211a);

    YansWifiPhyHelper wifiPhyB;
    wifiPhyB.Set("TxPowerStart", DoubleValue(Pdbm));
    wifiPhyB.Set("TxPowerEnd", DoubleValue(Pdbm));
    wifiPhyB.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannelB;
    wifiChannelB.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannelB.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(46.7) 
                                  );
    wifiChannelB.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                   "m0", DoubleValue(1.0), 
                                   "m1", DoubleValue(1.0),
                                   "m2", DoubleValue(1.0));

    wifiPhyB.SetChannel(wifiChannelB.Create());

    WifiMacHelper wifiMacB;
    wifiB.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                  "DataMode", StringValue(phyModeB),
                                  "ControlMode", StringValue(phyModeB));
    wifiMacB.SetType("ns3::AdhocWifiMac");

    // Install Net B на всех узлах (HAP и Ground B)
    NetDeviceContainer devicesB;
    devicesB.Add(wifiB.Install(wifiPhyB, wifiMacB, nodes.Get(0))); // HAP
    devicesB.Add(wifiB.Install(wifiPhyB, wifiMacB, nodes.Get(2))); // Ground B

    // --- НАСТРОЙКА АНТЕНН ДЛЯ NETWORK B ---

    // 1. Настраиваем HAP (Node 0) - Направленная антенна
    Ptr<WifiNetDevice> devB_Hap = DynamicCast<WifiNetDevice> (devicesB.Get(0));
    Ptr<CosineAntennaModel> antennaB_Hap = CreateObject<CosineAntennaModel>();
    antennaB_Hap->SetAttribute("Beamwidth", DoubleValue(60.0));
    antennaB_Hap->SetAttribute("MaxGain", DoubleValue(antGain));
    antennaB_Hap->SetAttribute("Orientation", DoubleValue(180.0)); 
    devB_Hap->GetPhy()->SetAttribute("Antenna", PointerValue(antennaB_Hap));
    g_hapAntennaB = antennaB_Hap;

    // 2. Настраиваем Ground B (Node 2) - Изотропная антенна
    Ptr<WifiNetDevice> devB_Ground = DynamicCast<WifiNetDevice> (devicesB.Get(1));
    Ptr<IsotropicAntennaModel> antennaB_Ground = CreateObject<IsotropicAntennaModel>();
    antennaB_Ground->SetAttribute("Gain", DoubleValue(0.0));
    devB_Ground->GetPhy()->SetAttribute("Antenna", PointerValue(antennaB_Ground));


    // --- Mobility Setup ---
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    
    // Node 0 (HAP): Начальная позиция на круге (радиус 6000м)
    // Начинаем с точки (Radius, 0, Height)
    positionAlloc->Add(Vector(g_circleRadius, 0.0, hight));
    
    // Node 1 (Ground A): На земле, слева от центра
    positionAlloc->Add(Vector(-groundDistance/2, 0.0, 0.0));
    
    // Node 2 (Ground B): На земле, справа от центра
    positionAlloc->Add(Vector(groundDistance/2, 0.0, 0.0));

    mobility.SetPositionAllocator(positionAlloc);
    
    // Наземные станции - статичны
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes.Get(1));
    mobility.Install(nodes.Get(2));

    // HAP использует ConstantVelocityMobilityModel для управления скоростью
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes.Get(0));

    // Сохраняем указатель на MobilityModel HAP для обновлений
    g_hapMobility = nodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    
    // Запускаем процесс управления HAP (позиция + антенна)
    Simulator::Schedule(Seconds(0.1), &UpdateHapState);


    // --- Internet Stack & IP ---
    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;

    // Assign IP for Network A (10.1.1.0/24)
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesA = ipv4.Assign(devicesA);

    // Assign IP for Network B (10.1.2.0/24)
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesB = ipv4.Assign(devicesB);

    // --- Routing Setup ---
    // 1. Enable IP Forwarding on HAP (Node 0)
    Ptr<Ipv4> ipv4Hap = nodes.Get(0)->GetObject<Ipv4>();
    ipv4Hap->SetAttribute("IpForward", BooleanValue(true));

    // 2. Configure Static Routes
    Ipv4StaticRoutingHelper staticRouting;

    // Route for Ground A (Node 1) to reach Network B (via HAP)
    Ptr<Ipv4> ipv4Node1 = nodes.Get(1)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingNode1 = staticRouting.GetStaticRouting(ipv4Node1);
    staticRoutingNode1->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), 
                                           Ipv4Mask("255.255.255.0"), 
                                           interfacesA.GetAddress(0), 
                                           1);

    // Route for Ground B (Node 2) to reach Network A (via HAP)
    Ptr<Ipv4> ipv4Node2 = nodes.Get(2)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingNode2 = staticRouting.GetStaticRouting(ipv4Node2);
    staticRoutingNode2->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), 
                                           Ipv4Mask("255.255.255.0"), 
                                           interfacesB.GetAddress(0), 
                                           1);

    // --- Application Setup ---
    uint16_t port = 9;
    
    // Sink on Node 2 (Ground B)
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(2), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

    // Source on Node 1 (Ground A)
    Ptr<Socket> source = Socket::CreateSocket(nodes.Get(1), tid);
    InetSocketAddress remote = InetSocketAddress(interfacesB.GetAddress(1), port);
    source->Connect(remote);

    // --- Tracing ---
    wifiPhyA.EnablePcap("wifi-simple-hap-netA", devicesA);
    wifiPhyB.EnablePcap("wifi-simple-hap-netB", devicesB);

    NS_LOG_UNCOND("Testing " << numPackets << " packets sent from Ground A (2.4GHz) to Ground B (5GHz) via Moving HAP");
    NS_LOG_UNCOND("HAP Height: " << hight << " m");
    NS_LOG_UNCOND("HAP Circle Radius: " << g_circleRadius << " m");
    NS_LOG_UNCOND("Ground Separation: " << groundDistance << " m");
    NS_LOG_UNCOND("HAP uses Directional Antennas (60 deg beamwidth) tracking ground.");

    Simulator::ScheduleWithContext(source->GetNode()->GetId(),
            Seconds(1.0),
            &GenerateTraffic,
            source,
            packetSize,
            numPackets,
            interPacketInterval);

    // --- FlowMonitor ---
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(120.0)); 
    Simulator::Run();

    // --- Statistics ---
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n\n--- SIMULATION RESULTS ---\n";
    std::cout << "Topology: Ground A <-> HAP (Moving Circle) <-> Ground B\n";
    std::cout << "Conditions\n";
    std::cout << "  Packet size: " << packetSize << " bytes\n";
    std::cout << "  HAP height: " << hight << " m\n";
    std::cout << "  Tx Power: " << Pdbm << " dBm\n";
    
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        std::cout << "\nFlow " << i->first 
            << " (" << t.sourceAddress << ":" << t.sourcePort << " -> " 
            << t.destinationAddress << ":" << t.destinationPort << ")\n";

        std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
        std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";

        if (i->second.txPackets > 0)
        {
            double lostPackets = i->second.txPackets - i->second.rxPackets;
            double lossRatio = (lostPackets / i->second.txPackets) * 100.0;
            std::cout << "  Lost Packets: " << lostPackets << " (" << lossRatio << "%)\n";
        }

        if (i->second.rxPackets > 0)
        {
             double throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds());
             std::cout << "  Throughput: " << throughput / 1024 << " Kbps\n";
             double delay = i->second.delaySum.GetSeconds() / i->second.rxPackets;
             std::cout << "  Avg Delay:  " << delay * 1000 << " ms\n";
        }
    }

    monitor->SerializeToXmlFile("hap-results-moving-beam.xml", true, true);
    std::cout << "-----------------------------\n\n";

    Simulator::Destroy();

    return 0;
}
