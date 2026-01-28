/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// Scenario: HAP (High Altitude Platform) with dual interfaces acts as a router.
// Network A: 2.4GHz (802.11b) connecting HAP and Ground Terminal A.
// Network B: 5GHz  (802.11a) connecting HAP and Ground Terminal B.
// MODIFICATION: HAP moves in a circle and uses directional gain to track ground.
// IMPLEMENTATION: We simulate directional antennas by dynamically adjusting TxGain/RxGain
// attributes on YansWifiPhy based on the angle between HAP and the Ground Station.

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiHapDualBandMoving");

// --- Глобальные переменные ---
Ptr<ConstantVelocityMobilityModel> g_hapMobility;
Ptr<YansWifiPhy> g_phyHapA;
Ptr<YansWifiPhy> g_phyHapB;
Ptr<MobilityModel> g_mobilityNodeA;
Ptr<MobilityModel> g_mobilityNodeB;

// Параметры кругового движения и антенны
double g_circleRadius = 6000.0; 
double g_angularVelocity = 2 * M_PI / 100.0; 
double g_maxAntennaGain = 20.0; // dBi
double g_beamwidthExponent = 2.0; // Степень для моделирования ширины луча

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

// Вспомогательная функция для расчета вектора
Vector GetVector(Vector from, Vector to)
{
    return Vector(to.x - from.x, to.y - from.y, to.z - from.z);
}

// Функция расчета угла между двумя векторами (в радианах)
double CalculateAngle(Vector v1, Vector v2)
{
    double dotProduct = v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    double mag1 = std::sqrt(v1.x * v1.x + v1.y * v1.y + v1.z * v1.z);
    double mag2 = std::sqrt(v2.x * v2.x + v2.y * v2.y + v2.z * v2.z);

    if (mag1 * mag2 == 0) return 0.0;

    double cosAngle = dotProduct / (mag1 * mag2);
    // Ограничиваем диапазон, чтобы избежать ошибок округления
    if (cosAngle > 1.0) cosAngle = 1.0;
    if (cosAngle < -1.0) cosAngle = -1.0;

    return std::acos(cosAngle);
}

// Расчет усиления на основе угла отклонения от центра луча (Cosine Antenna Model approximation)
double CalculateDirectionalGain(double angleRad)
{
    // Модель: Gain = MaxGain + 10 * log10(cos(angle)^exponent)
    double cosAngle = std::cos(angleRad);
    if (cosAngle < 0.0) cosAngle = 0.0; // Падение за лепесток
    
    // Для избежания ошибки log10(0)
    if (cosAngle < 0.01) return -20.0; // Минимальное усиление

    double gain = g_maxAntennaGain + 10.0 * std::log10(std::pow(cosAngle, g_beamwidthExponent));
    return gain;
}

// --- Функция обновления позиции HAP и усиления антенны ---
void UpdateHapState()
{
    if (!g_hapMobility || !g_phyHapA || !g_phyHapB)
    {
        return;
    }

    // 1. Обновление вектора скорости (движение по кругу)
    Vector hapPos = g_hapMobility->GetPosition();
    double vx = -g_angularVelocity * hapPos.y;
    double vy =  g_angularVelocity * hapPos.x;
    g_hapMobility->SetVelocity(Vector(vx, vy, 0.0));

    // Вектор направления "взгляда" HAP (в центр круга 0,0,h)
    // HAP смотрит векторно на точку (0,0,0).
    Vector viewVector = GetVector(hapPos, Vector(0.0, 0.0, 0.0));

    // 2. Расчет усиления для Network A (HAP <-> Ground A)
    Vector vecHapToA = GetVector(hapPos, g_mobilityNodeA->GetPosition());
    double angleA = CalculateAngle(viewVector, vecHapToA);
    double gainA = CalculateDirectionalGain(angleA);
    
    g_phyHapA->SetAttribute("TxGain", DoubleValue(gainA));
    g_phyHapA->SetAttribute("RxGain", DoubleValue(gainA));

    // 3. Расчет усиления для Network B (HAP <-> Ground B)
    Vector vecHapToB = GetVector(hapPos, g_mobilityNodeB->GetPosition());
    double angleB = CalculateAngle(viewVector, vecHapToB);
    double gainB = CalculateDirectionalGain(angleB);

    g_phyHapB->SetAttribute("TxGain", DoubleValue(gainB));
    g_phyHapB->SetAttribute("RxGain", DoubleValue(gainB));

    NS_LOG_DEBUG("HAP Update: GainA=" << gainA << " dB, GainB=" << gainB << " dB");

    Simulator::Schedule(Seconds(0.1), &UpdateHapState);
}

int main(int argc, char* argv[])
{
    std::string phyModeA("DsssRate1Mbps");
    std::string phyModeB("OfdmRate6Mbps");
    uint32_t packetSize{1000};
    uint32_t numPackets{10};
    Time interPacketInterval{"40ms"};
    bool verbose{false};

    double hight{20000.0};    
    double Pdbm{20.0};       // Исправлено: добавлено }
    double antGain{20.0};    // Исправлено: добавлено }

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

    g_maxAntennaGain = antGain;

    NodeContainer nodes;
    nodes.Create(3);

    // --- Network A Setup (YansWifiPhy) ---
    WifiHelper wifiA;
    if (verbose)
        WifiHelper::EnableLogComponents();
    wifiA.SetStandard(WIFI_STANDARD_80211b);

    YansWifiPhyHelper wifiPhyA;
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
    
    // Install Net A
    NetDeviceContainer devicesA;
    devicesA.Add(wifiA.Install(wifiPhyA, wifiMacA, nodes.Get(0))); // HAP
    devicesA.Add(wifiA.Install(wifiPhyA, wifiMacA, nodes.Get(1))); // Ground A

    // Сохраняем указатель на PHY HAP для управления Gain
    g_phyHapA = DynamicCast<YansWifiPhy> (DynamicCast<WifiNetDevice>(devicesA.Get(0))->GetPhy());


    // --- Network B Setup (YansWifiPhy) ---
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

    NetDeviceContainer devicesB;
    devicesB.Add(wifiB.Install(wifiPhyB, wifiMacB, nodes.Get(0))); // HAP
    devicesB.Add(wifiB.Install(wifiPhyB, wifiMacB, nodes.Get(2))); // Ground B

    // Сохраняем указатель на PHY HAP для управления Gain
    g_phyHapB = DynamicCast<YansWifiPhy> (DynamicCast<WifiNetDevice>(devicesB.Get(0))->GetPhy());


    // --- Mobility Setup ---
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    
    positionAlloc->Add(Vector(g_circleRadius, 0.0, hight));
    positionAlloc->Add(Vector(-groundDistance/2, 0.0, 0.0));
    positionAlloc->Add(Vector(groundDistance/2, 0.0, 0.0));

    mobility.SetPositionAllocator(positionAlloc);
    
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes.Get(1));
    mobility.Install(nodes.Get(2));

    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes.Get(0));

    g_hapMobility = nodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    
    // Сохраняем модели мобильности наземных станций для расчета углов
    g_mobilityNodeA = nodes.Get(1)->GetObject<MobilityModel>();
    g_mobilityNodeB = nodes.Get(2)->GetObject<MobilityModel>();

    Simulator::Schedule(Seconds(0.1), &UpdateHapState);


    // --- Internet Stack & IP ---
    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;

    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesA = ipv4.Assign(devicesA);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer interfacesB = ipv4.Assign(devicesB);

    // --- Routing Setup ---
    Ptr<Ipv4> ipv4Hap = nodes.Get(0)->GetObject<Ipv4>();
    ipv4Hap->SetAttribute("IpForward", BooleanValue(true));

    Ipv4StaticRoutingHelper staticRouting;

    Ptr<Ipv4> ipv4Node1 = nodes.Get(1)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingNode1 = staticRouting.GetStaticRouting(ipv4Node1);
    staticRoutingNode1->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), 
                                           Ipv4Mask("255.255.255.0"), 
                                           interfacesA.GetAddress(0), 
                                           1);

    Ptr<Ipv4> ipv4Node2 = nodes.Get(2)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingNode2 = staticRouting.GetStaticRouting(ipv4Node2);
    staticRoutingNode2->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), 
                                           Ipv4Mask("255.255.255.0"), 
                                           interfacesB.GetAddress(0), 
                                           1);

    // --- Application Setup ---
    uint16_t port = 9;
    
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(2), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

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
    NS_LOG_UNCOND("Using Simulated Directional Antenna (Dynamic Gain on YansWifiPhy).");

    Simulator::ScheduleWithContext(source->GetNode()->GetId(),
            Seconds(1.0),
            &GenerateTraffic,
            source,
            packetSize,
            numPackets,
            interPacketInterval);

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
