/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// Scenario: Two groups of HAP-connected terminals linked via GEO Satellite.
// - Ground-HAP Link: Uses WiFi (AdHoc mode).
// - HAP-Satellite Link: Uses Wireless Channel with LogDistance + Nakagami Fading.
// - Traffic flows from Group 1 to Group 2 via the satellite backbone.

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
// #include "ns3/point-to-point-module.h" // Больше не нужен, так как используем беспроводной канал
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

// Log component definition
NS_LOG_COMPONENT_DEFINE ("SatelliteHapTwoGroups");

// --- Node Indices Enumeration ---
enum NodeIndices {
    // Group 1
    HAP_1   = 0,
    UT_1_1,
    UT_1_2,
    // Group 2
    HAP_2,
    UT_2_1,
    UT_2_2,
    // Satellite
    SATELLITE
};

// --- Traffic Generation Callbacks ---

void ReceivePacket(Ptr<Socket> socket)
{
    while (socket->Recv())
    {
        // Silently receive
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

int 
main (int argc, char *argv[])
{
  // --- General Parameters ---
  std::string phyModeA("DsssRate1Mbps"); // 802.11b
  std::string phyModeB("OfdmRate6Mbps"); // 802.11a
  std::string satPhyMode("OfdmRate6Mbps"); // 802.11a for space link
  uint32_t packetSize{1000}; // bytes
  uint32_t numPackets{10};
  Time interPacketInterval{"40ms"};
  bool verbose = false;
  
  // HAP Parameters
  double hight{20000.0};    // meters
  double Pdbm{20.};       // Transmitter power (dBm) for Ground/HAP WiFi
  
  // Antenna Gain
  double antGain{20.};    // WiFi Antenna gain (dB)
  double satAntGain{40.}; // Satellite antenna Gain (dB)

  // Ground separation (distance between terminal A and B on the ground)
  double groundDistance{5000.0};
  
  // Groups ground separation (distance between terminal groups)
  double groupDistance{100000.0};
  
  // Satellite-ground distance
  double satelliteDistance{35786000.0};

  // Satellite Link Specific Parameters
  double satTxPower{40.0}; // Power of Satellite/HAP transmitters for space link (dBm)

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
  cmd.AddValue("groupDistance", "Distance between groups of terminals (m)", groupDistance);
  cmd.AddValue("satTxPower", "Power for Satellite link (dBm)", satTxPower);
  cmd.Parse(argc, argv);

  // --- 1. Create Nodes ---
  NodeContainer nodes;
  nodes.Create (7);

  // --- 2. Configure WiFi Channel Helpers (Ground Links) ---

  // --- WiFi for Group 1 (2.4 GHz, 802.11b) ---
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
                                   "ReferenceLoss", DoubleValue(40.0));
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

  // --- WiFi for Group 2 (5 GHz, 802.11a) ---
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
  wifiChannelB.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(46.7));
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

  // --- 3. Configure Satellite Wireless Link ---
  WifiHelper wifiSat;
  wifiSat.SetStandard(WIFI_STANDARD_80211a); // Using 5GHz as a base.

  YansWifiPhyHelper wifiPhySat;
  wifiPhySat.Set("TxGain", DoubleValue(satAntGain)); 
  wifiPhySat.Set("RxGain", DoubleValue(satAntGain));
  wifiPhySat.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySat.Set("TxPowerEnd", DoubleValue(satTxPower));
  wifiPhySat.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

  YansWifiChannelHelper wifiChannelSat;
  wifiChannelSat.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  
  // --- Accounting for distance losses and fading for the Satellite ---
  // LogDistance: Model for free space (Exponent=2.0)
  // ReferenceLoss: For 5GHz at 1m approximately 46.7 dB (as in Group 2)
  wifiChannelSat.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(46.7));

  wifiChannelSat.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                   "m0", DoubleValue(1.0), 
                                   "m1", DoubleValue(1.0),
                                   "m2", DoubleValue(1.0));

  wifiPhySat.SetChannel(wifiChannelSat.Create());

  WifiMacHelper wifiMacSat;
  wifiSat.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue(satPhyMode),
                              "ControlMode", StringValue(satPhyMode));
  wifiMacSat.SetType("ns3::AdhocWifiMac"); // Adhoc mode for link HAP-Sat


  // --- 4. Install NetDevices ---

  // Install WiFi for Group 1
  NetDeviceContainer wifiDevicesA;
  wifiDevicesA = wifiA.Install(wifiPhyA, wifiMacA, NodeContainer(nodes.Get(HAP_1), nodes.Get(UT_1_1), nodes.Get(UT_1_2)));

  // Install WiFi for Group 2
  NetDeviceContainer wifiDevicesB;
  wifiDevicesB = wifiB.Install(wifiPhyB, wifiMacB, NodeContainer(nodes.Get(HAP_2), nodes.Get(UT_2_1), nodes.Get(UT_2_2)));

  // Install Satellite Wireless Devices (HAP 1, HAP 2, Satellite in one network)
  // we set them all to the same subnet so they can route traffic through each other
  NetDeviceContainer satDevices;
  satDevices = wifiSat.Install(wifiPhySat, wifiMacSat, NodeContainer(nodes.Get(HAP_1), nodes.Get(SATELLITE), nodes.Get(HAP_2)));


  // --- 5. Install Internet Stack ---
  InternetStackHelper stack;
  stack.Install (nodes);

  // --- 6. Assign IP Addresses ---
  Ipv4AddressHelper address;
  
  // Network for Group 1 WiFi (10.1.1.0/24)
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiA = address.Assign (wifiDevicesA);

  // Network for Group 2 WiFi (10.1.2.0/24)
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiB = address.Assign (wifiDevicesB);

  // Network for Satellite Wireless (10.1.3.0/24)
  // All three devices (HAP1, Sat, HAP2) are now in the same
  // broadcast domain, like in AdHoc
  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSat = address.Assign (satDevices);
  
  // Примечание: satDevices.Get(0) - это HAP_1, Get(1) - SATELLITE, Get(2) - HAP_2

  // --- 7. Mobility Setup ---
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

  // Group 1 Positions (Center at 0,0)
  positionAlloc->Add(Vector(0.0, 0.0, hight));     // HAP 1
  positionAlloc->Add(Vector(-groundDistance/2, 0.0, 0.0));  // UT 1_1
  positionAlloc->Add(Vector(groundDistance/2.0, 0.0, 0.0)); // UT 1_2

  // Group 2 Positions (Center at groupDistance, 6000)
  positionAlloc->Add(Vector(groupDistance, 6000.0, hight)); // HAP 2
  positionAlloc->Add(Vector(groupDistance - groundDistance/2, 6000.0, 0.0)); // UT 2_1
  positionAlloc->Add(Vector(groupDistance + groundDistance/2, 6000.0, 0.0)); // UT 2_2

  // Satellite Position
  positionAlloc->Add(Vector(satelliteDistance, 0.0, 0.0)); // Satellite (GEO)

  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  // --- 8. Routing ---
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // --- 9. Applications (Sockets) ---
  uint16_t port = 9;
  
  // Create Receiver on Group 2 User (UT_2_1)
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(UT_2_1), tid);
  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
  recvSink->Bind(local);
  recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

  // Create Source on Group 1 User (UT_1_1)
  Ptr<Socket> source = Socket::CreateSocket(nodes.Get(UT_1_1), tid);
  InetSocketAddress remote = InetSocketAddress(interfacesWifiB.GetAddress(1), port);
  source->Connect(remote);

  // --- 10. Flow Monitor Setup ---
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Start Traffic
  NS_LOG_UNCOND("Testing " << numPackets << " packets sent from Group 1 to Group 2 via GEO Satellite");
  NS_LOG_UNCOND("Link: WiFi (Ground) <-> Wireless Sat Channel (LogDistance+Nakagami) <-> WiFi (Ground)");
  
  Simulator::ScheduleWithContext(source->GetNode()->GetId(),
          Seconds(1.0),
          &GenerateTraffic,
          source,
          packetSize,
          numPackets,
          interPacketInterval);

  // Run Simulation
  double simTime = 1.0 + (numPackets * interPacketInterval.GetSeconds()) + 5.0; 
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // --- Statistics ---
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n\n--- SIMULATION RESULTS ---\n";
  std::cout << "Topology: Group 1 (WiFi) <-> HAP 1 <-> GEO Sat <-> HAP 2 <-> Group 2 (WiFi)\n";
  std::cout << "Conditions:\n";
  std::cout << "  Packet size: " << packetSize << " bytes\n";
  std::cout << "  Number of packets: " << numPackets << "\n";
  std::cout << "  Interval: " << interPacketInterval.GetMilliSeconds() << " ms\n";
  std::cout << "  HAP height: " << hight << " m\n";
  std::cout << "  Tx Power (Ground): " << Pdbm << " dBm\n";
  std::cout << "  Tx Power (Sat Link): " << satTxPower << " dBm\n";
  std::cout << "  Ant Gain (Sat): " << satAntGain << " dBi\n";
  std::cout << "  Propagation Model: LogDistance + Nakagami (m=1)\n";

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

  monitor->SerializeToXmlFile("hap-sat-wifi-stats.xml", true, true);
  std::cout << "-----------------------------\n\n";

  Simulator::Destroy();
  
  return 0;
}