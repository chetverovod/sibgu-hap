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

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"

using namespace ns3;
enum {HAP, UT_A, UT_B};
NS_LOG_COMPONENT_DEFINE("WifiHapDualBand");

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
    cmd.AddValue("antGain", "Antenna gain for transmitter and reciever, (dB)", antGain);
    cmd.AddValue("groundDistance", "Distance between ground terminals A and B (m)", groundDistance);
    cmd.Parse(argc, argv);

    // NOTE: We do NOT set NonUnicastMode globally here because Network A uses DSSS 
    // and Network B uses OFDM. Setting it globally would cause a crash when 
    // the 5GHz node tries to send a broadcast packet using a DSSS rate.

    // Create Nodes
    // Node 0: HAP
    // Node 1: Ground Terminal A (Network A)
    // Node 2: Ground Terminal B (Network B)
    NodeContainer nodes;
    nodes.Create(3);

    // --- Network A Setup (2.4 GHz, 802.11b) ---
    // Connecting HAP (Node 0) <-> Ground A (Node 1)
    WifiHelper wifiA;
    if (verbose)
        WifiHelper::EnableLogComponents();
    wifiA.SetStandard(WIFI_STANDARD_80211b);

    YansWifiPhyHelper wifiPhyA;
    wifiPhyA.Set("TxGain", DoubleValue(antGain));
    wifiPhyA.Set("RxGain", DoubleValue(antGain));
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
    
    // Install Net A on HAP and Ground A
    NetDeviceContainer devicesA;
    devicesA.Add(wifiA.Install(wifiPhyA, wifiMacA, nodes.Get(HAP))); // HAP
    devicesA.Add(wifiA.Install(wifiPhyA, wifiMacA, nodes.Get(UT_A))); // Ground A


    // --- Network B Setup (5 GHz, 802.11a) ---
    // Connecting HAP (Node 0) <-> Ground B (Node 2)
    WifiHelper wifiB;
    wifiB.SetStandard(WIFI_STANDARD_80211a);

    YansWifiPhyHelper wifiPhyB;
    wifiPhyB.Set("TxGain", DoubleValue(antGain));
    wifiPhyB.Set("RxGain", DoubleValue(antGain));
    wifiPhyB.Set("TxPowerStart", DoubleValue(Pdbm));
    wifiPhyB.Set("TxPowerEnd", DoubleValue(Pdbm));
    wifiPhyB.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannelB;
    wifiChannelB.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    // Frequency for 5GHz ~ 5.0 or 5.9 GHz
    wifiChannelB.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(46.7) // Approx ref loss for 5GHz
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

    // Install Net B on HAP and Ground B
    NetDeviceContainer devicesB;
    devicesB.Add(wifiB.Install(wifiPhyB, wifiMacB, nodes.Get(HAP))); // HAP
    devicesB.Add(wifiB.Install(wifiPhyB, wifiMacB, nodes.Get(UT_B))); // Ground B


    // --- Mobility Setup ---
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    
    // Node 0 (HAP): High above
    positionAlloc->Add(Vector(0.0, 0.0, hight));
    
    // Node 1 (Ground A): On ground, at 0,0
    positionAlloc->Add(Vector(-groundDistance/2, 0.0, 0.0));
    
    // Node 2 (Ground B): On ground, separated by groundDistance
    positionAlloc->Add(Vector(groundDistance/2, 0.0, 0.0));

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);


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
    Ptr<Ipv4> ipv4Hap = nodes.Get(HAP)->GetObject<Ipv4>();
    ipv4Hap->SetAttribute("IpForward", BooleanValue(true));

    // 2. Configure Static Routes
    Ipv4StaticRoutingHelper staticRouting;

    // Route for Ground A (Node 1) to reach Network B (via HAP)
    // Destination: 10.1.2.0/24, Gateway: 10.1.1.1 (HAP), Interface: Network A interface of Node 1
    Ptr<Ipv4> ipv4Node1 = nodes.Get(UT_A)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingNode1 = staticRouting.GetStaticRouting(ipv4Node1);
    staticRoutingNode1->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), 
                                           Ipv4Mask("255.255.255.0"), 
                                           interfacesA.GetAddress(0), // HAP's IP in Net A
                                           1); // Interface index (likely 1, as 0 is loopback)

    // Route for Ground B (Node 2) to reach Network A (via HAP)
    // Destination: 10.1.1.0/24, Gateway: 10.1.2.1 (HAP), Interface: Network B interface of Node 2
    Ptr<Ipv4> ipv4Node2 = nodes.Get(UT_B)->GetObject<Ipv4>();
    Ptr<Ipv4StaticRouting> staticRoutingNode2 = staticRouting.GetStaticRouting(ipv4Node2);
    staticRoutingNode2->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), 
                                           Ipv4Mask("255.255.255.0"), 
                                           interfacesB.GetAddress(0), // HAP's IP in Net B
                                           1); // Interface index

    // --- Application Setup ---
    // Flow: Ground A (Node 1) -> Ground B (Node 2)
    uint16_t port = 9;
    
    // Sink on Node 2 (Ground B)
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(UT_B), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
    recvSink->Bind(local);
    recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

    // Source on Node 1 (Ground A)
    Ptr<Socket> source = Socket::CreateSocket(nodes.Get(UT_A), tid);
    InetSocketAddress remote = InetSocketAddress(interfacesB.GetAddress(1), port); // Dest IP of Node 2
    source->Connect(remote);

    // --- Tracing ---
    // Enable PCAP for both interfaces
    wifiPhyA.EnablePcap("wifi-simple-hap-netA", devicesA);
    wifiPhyB.EnablePcap("wifi-simple-hap-netB", devicesB);

    NS_LOG_UNCOND("Testing " << numPackets << " packets sent from Ground A (2.4GHz) to Ground B (5GHz) via HAP");
    NS_LOG_UNCOND("HAP Height: " << hight << " m");
    NS_LOG_UNCOND("Ground Separation: " << groundDistance << " m");

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

    Simulator::Stop(Seconds(44.0));
    Simulator::Run();

    // --- Statistics ---
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n\n--- SIMULATION RESULTS ---\n";
    std::cout << "Topology: Ground A (Node 1) <-> HAP (Node 0) <-> Ground B (Node 2)\n";
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

    monitor->SerializeToXmlFile("hap-results-dual-band.xml", true, true);
    std::cout << "-----------------------------\n\n";

    Simulator::Destroy();

    return 0;
}
