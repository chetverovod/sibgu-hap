/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// Scenario: Two groups of HAP-connected terminals linked via GEO Satellite.
// - Ground-HAP Link: Uses WiFi (AdHoc mode), similar to wifi-moving-hap-router.cc.
// - HAP-Satellite Link: Uses PointToPoint (High latency).
// - Traffic flows from Group 1 to Group 2 via the satellite backbone.

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
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
  //bool verbose = false;
  uint32_t packetSize = 1000; // bytes
  uint32_t numPackets = 1000;
  Time interPacketInterval = MilliSeconds(10); 
  
  double hapHeight = 20000.0; // 20 km altitude for HAP

  // --- 1. Create Nodes ---
  
  // Total nodes: 2 HAPs + 4 Users + 1 Satellite
  NodeContainer nodes;
  nodes.Create (7);

  // --- 2. Configure WiFi Channel Helpers (Based on wifi-moving-hap-router.cc) ---

  // --- WiFi for Group 1 (2.4 GHz, 802.11b) ---
  WifiHelper wifiA;
  wifiA.SetStandard(WIFI_STANDARD_80211b);

  YansWifiPhyHelper wifiPhyA;
  wifiPhyA.Set("TxPowerStart", DoubleValue(30.0)); // High power for HAP
  wifiPhyA.Set("TxPowerEnd", DoubleValue(30.0));
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
                                "DataMode", StringValue("DsssRate1Mbps"),
                                "ControlMode", StringValue("DsssRate1Mbps"));
  wifiMacA.SetType("ns3::AdhocWifiMac");

  // --- WiFi for Group 2 (5 GHz, 802.11a) ---
  WifiHelper wifiB;
  wifiB.SetStandard(WIFI_STANDARD_80211a);

  YansWifiPhyHelper wifiPhyB;
  wifiPhyB.Set("TxPowerStart", DoubleValue(30.0));
  wifiPhyB.Set("TxPowerEnd", DoubleValue(30.0));
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
                                "DataMode", StringValue("OfdmRate6Mbps"),
                                "ControlMode", StringValue("OfdmRate6Mbps"));
  wifiMacB.SetType("ns3::AdhocWifiMac");

  // --- P2P Satellite Link (HAP <-> GEO Satellite) ---
  PointToPointHelper p2pSatellite;
  p2pSatellite.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  p2pSatellite.SetChannelAttribute ("Delay", StringValue ("130ms"));

  // --- 3. Install NetDevices ---

  // Install WiFi for Group 1 (HAP 1 + Users 1)
  // Note: HAP needs dual interface: one WiFi (ground), one P2P (sat)
  NetDeviceContainer wifiDevicesA;
  wifiDevicesA = wifiA.Install(wifiPhyA, wifiMacA, NodeContainer(nodes.Get(HAP_1), nodes.Get(UT_1_1), nodes.Get(UT_1_2)));

  // Install WiFi for Group 2 (HAP 2 + Users 2)
  NetDeviceContainer wifiDevicesB;
  wifiDevicesB = wifiB.Install(wifiPhyB, wifiMacB, NodeContainer(nodes.Get(HAP_2), nodes.Get(UT_2_1), nodes.Get(UT_2_2)));

  // Install Satellite P2P Links
  NetDeviceContainer satLink1;
  satLink1 = p2pSatellite.Install (nodes.Get (HAP_1), nodes.Get (SATELLITE));

  NetDeviceContainer satLink2;
  satLink2 = p2pSatellite.Install (nodes.Get (SATELLITE), nodes.Get (HAP_2));

  // --- 4. Install Internet Stack ---
  InternetStackHelper stack;
  stack.Install (nodes);

  // --- 5. Assign IP Addresses ---
  
  Ipv4AddressHelper address;
  
  // Network for Group 1 WiFi (10.1.1.0/24)
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiA = address.Assign (wifiDevicesA);

  // Network for Group 2 WiFi (10.1.2.0/24)
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiB = address.Assign (wifiDevicesB);

  // Network HAP 1 <-> Satellite (10.1.3.0/30)
  address.SetBase ("10.1.3.0", "255.255.255.252");
  Ipv4InterfaceContainer interfacesSat1 = address.Assign (satLink1);

  // Network Satellite <-> HAP 2 (10.1.4.0/30)
  address.SetBase ("10.1.4.0", "255.255.255.252");
  Ipv4InterfaceContainer interfacesSat2 = address.Assign (satLink2);

  // --- 6. Mobility Setup (Crucial for WiFi) ---
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

  // Group 1 Positions (Center at 0,0)
  positionAlloc->Add(Vector(0.0, 0.0, hapHeight));     // HAP 1
  positionAlloc->Add(Vector(0.0, 0.0, 0.0));          // UT 1_1
  positionAlloc->Add(Vector(500.0, 0.0, 0.0));        // UT 1_2

  // Group 2 Positions (Center at 6000,6000 to separate from Group 1)
  positionAlloc->Add(Vector(6000.0, 6000.0, hapHeight)); // HAP 2
  positionAlloc->Add(Vector(6000.0, 6000.0, 0.0));       // UT 2_1
  positionAlloc->Add(Vector(6500.0, 6000.0, 0.0));      // UT 2_2

  // Satellite Position (Far away in space, doesn't matter much for P2P logic but for completeness)
  positionAlloc->Add(Vector(35786000.0, 0.0, 0.0));     // Satellite (approx GEO distance)

  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);


  // --- 7. Routing ---
  // Note: HAPs have multiple interfaces (WiFi + P2P). 
  // Global Routing is required to forward packets between WiFi and P2P interfaces.
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // --- 8. Applications (Sockets) ---
  
  uint16_t port = 9;
  
  // Create Receiver on Group 2 User (UT_2_1)
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(UT_2_1), tid);
  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
  recvSink->Bind(local);
  recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

  // Create Source on Group 1 User (UT_1_1)
  Ptr<Socket> source = Socket::CreateSocket(nodes.Get(UT_1_1), tid);
  // Get the address of UT_2_1 from the WiFi interface container
  InetSocketAddress remote = InetSocketAddress(interfacesWifiB.GetAddress(1), port); // interfacesWifiB: 0=HAP2, 1=UT2_1, 2=UT2_2
  source->Connect(remote);

  // --- 9. Flow Monitor Setup ---
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Start Traffic
  NS_LOG_UNCOND("Testing " << numPackets << " packets sent from Group 1 to Group 2 via GEO Satellite");
  NS_LOG_UNCOND("Link: WiFi (Ground) <-> P2P (Satellite) <-> WiFi (Ground)");
  
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

  // --- 10. Statistics Output ---
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n\n--- SIMULATION RESULTS ---\n";
  std::cout << "Topology: Group 1 (WiFi) <-> HAP 1 <-> GEO Sat <-> HAP 2 <-> Group 2 (WiFi)\n";
  std::cout << "Conditions:\n";
  std::cout << "  Packet size: " << packetSize << " bytes\n";
  std::cout << "  Number of packets: " << numPackets << "\n";
  std::cout << "  Interval: " << interPacketInterval.GetMilliSeconds() << " ms\n";
  std::cout << "  Satellite Delay: 130 ms (one way)\n";

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