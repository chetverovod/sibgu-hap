/*
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

// Scenario: Two groups of HAP-connected terminals linked via GEO Satellite using Ka-band.
// - Ground-HAP Link: Uses WiFi (AdHoc mode).
// - HAP-Satellite Link: Uses Ka-band Satellite Channel with specialized models.
// - Traffic flows from Group 1 to Group 2 via the satellite backbone.
//
// CORRECTIONS APPLIED (v2 - Separate Frequencies & Restored Stats):
// 1. Separated Uplink/Downlink channels for HAP_1 and HAP_2.
// 2. Assigned different frequencies: HAP_1 (30/28 GHz), HAP_2 (29/27 GHz).
// 3. Restored original Link Budget calculation block.
// 4. Restored original FlowMonitor statistics (Throughput, Delay, Jitter).
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/propagation-loss-model.h"
#include <map>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

// For work with WiFi headers
#include "ns3/wifi-mac-header.h"

using namespace ns3;

// Log component definition
NS_LOG_COMPONENT_DEFINE ("SatelliteHapKaBand");

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
// --- Global map for MAC address and node ID collation ---
std::map<Mac48Address, uint32_t> g_macToNodeId;

// --- Structure for storing stream statistics (Source -> Destination) ---
struct FlowLinkStats {
    uint32_t txPackets = 0;
    uint32_t rxPackets = 0;
    uint32_t rxDropped = 0;
};
// --- Global statistics map: Key = pair(SrcNodeId, DstNodeId) ---
std::map<std::pair<uint32_t, uint32_t>, FlowLinkStats> g_flowStats;

// --- Helper function for getting the node name by its ID ---
std::string GetNodeName(uint32_t id) {
    switch (id) {
        case HAP_1:   return "HAP_1";
        case UT_1_1:  return "UT_1_1";
        case UT_1_2:  return "UT_1_2";
        case HAP_2:   return "HAP_2";
        case UT_2_1:  return "UT_2_1";
        case UT_2_2:  return "UT_2_2";
        case SATELLITE: return "SAT";
        default:      return "Unknown";
    }
}

// --- Function for filling the MAC address Node ID mapping ---
void PopulateMacTable(NetDeviceContainer devices) {
    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        if (wifiDev) {
            Mac48Address addr = wifiDev->GetMac()->GetAddress();
            uint32_t nodeId = wifiDev->GetNode()->GetId();
            g_macToNodeId[addr] = nodeId;
        }
    }
}

// --- Callbacks for Link Monitoring (Flow Based) ---

void PhyTxBeginCallback(Ptr<NetDevice> device, Ptr<const Packet> packet, double txPower) {
    WifiMacHeader header;
    packet->PeekHeader(header);
    
    Mac48Address destAddr = header.GetAddr1();
    if (destAddr.IsGroup()) return; 

    auto it = g_macToNodeId.find(destAddr);
    if (it != g_macToNodeId.end()) {
        uint32_t srcId = device->GetNode()->GetId();
        uint32_t dstId = it->second;
        g_flowStats[std::make_pair(srcId, dstId)].txPackets++;
    }
}

void PhyRxDropCallback(Ptr<NetDevice> device, Ptr<const Packet> packet, WifiPhyRxfailureReason reason) {
    WifiMacHeader header;
    if (packet->PeekHeader(header)) {
        Mac48Address srcAddr = header.GetAddr2();
        auto it = g_macToNodeId.find(srcAddr);
        if (it != g_macToNodeId.end()) {
            uint32_t srcId = it->second;
            uint32_t dstId = device->GetNode()->GetId();
            g_flowStats[std::make_pair(srcId, dstId)].rxDropped++;
        }
    }
}

void PhyRxEndCallback(Ptr<NetDevice> device, Ptr<const Packet> packet) {
    WifiMacHeader header;
    // We try to read the header (it exists at the Phy level)
    if (packet->PeekHeader(header)) {
        Mac48Address destAddr = header.GetAddr1();
        Mac48Address myAddr = Mac48Address::ConvertFrom(device->GetAddress());

        // We only count a packet if it is addressed to us (or is a broadcast)
        if (destAddr == myAddr || destAddr.IsBroadcast()) {
            Mac48Address srcAddr = header.GetAddr2();
            
            // Find the sender node ID
            auto it = g_macToNodeId.find(srcAddr);
            if (it != g_macToNodeId.end()) {
                uint32_t srcId = it->second;
                uint32_t dstId = device->GetNode()->GetId();

                // Increment the RX counter for the Src -> Dst stream
                g_flowStats[std::make_pair(srcId, dstId)].rxPackets++;
            }
        }
    }
}

void SetupDeviceTraces(NetDeviceContainer devices) {
    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<NetDevice> dev = devices.Get(i);
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(dev);
        if (wifiDev) {
            // Tx trace
            wifiDev->GetPhy()->TraceConnectWithoutContext("PhyTxBegin",
                MakeBoundCallback(&PhyTxBeginCallback, dev));
            
            // Rx trace
            wifiDev->GetPhy()->TraceConnectWithoutContext("PhyRxEnd",
                MakeBoundCallback(&PhyRxEndCallback, dev));
            
            // Drop trace
            wifiDev->GetPhy()->TraceConnectWithoutContext("PhyRxDrop",
                MakeBoundCallback(&PhyRxDropCallback, dev));
        }
    }
}

// --- Traffic Generation Callbacks ---
void ReceivePacket(Ptr<Socket> socket)
{
    while (socket->Recv()) { }
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

std::string GetNodeNameByIp(Ipv4Address ip, NodeContainer nodes) {
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<Node> node = nodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (ipv4) {
            for (uint32_t j = 0; j < ipv4->GetNInterfaces(); ++j) {
                for (uint32_t k = 0; k < ipv4->GetNAddresses(j); ++k) {
                    if (ipv4->GetAddress(j, k).GetLocal() == ip) {
                        return GetNodeName(i);
                    }
                }
            }
        }
    }
    return "Unknown";
}

int 
main (int argc, char *argv[])
{
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/AdhocWifiMac/AckTimeout", TimeValue (MilliSeconds (300)));

// --- General Parameters ---
  std::string phyModeA("DsssRate1Mbps"); // 802.11b
  std::string phyModeB("OfdmRate6Mbps"); // 802.11a
  uint32_t packetSize{1500}; 
  uint32_t numPackets{1000};
  Time interPacketInterval{"265ms"};
  bool verbose = false;
  
  double hight{20000.0};    // meters
  double Pdbm{26.};       // WiFi TX Power (dBm)
  
  // Antenna Gains
  double antGain{32.};    // WiFi Antenna gain
  double satAntGain{50.}; // Satellite antenna Gain
  double hapSatAntGain{45.}; // HAP antenna Gain for Sat link

  double groundDistance{5000.0};
  double groupDistance{100000.0};
  double satelliteDistance{35786000.0};

  // Power settings
  double satTxPower{50.0}; 
  double hapSatTxPower{45.0}; 
  
  // Frequencies (GHz) - Distinct for HAP1 and HAP2
  double freqHap1Up{30.0e9};   // 30 GHz
  double freqHap1Down{28.0e9}; // 28 GHz
  double freqHap2Up{29.0e9};   // 29 GHz
  double freqHap2Down{27.0e9}; // 27 GHz
  
  // Atmospheric parameters
  double rainAttenuation{3.0}; 
  double oxygenAbsorption{0.1};
  double waterVaporAbsorption{0.05};
  double rainCloudHeight{5000.0}; 

  CommandLine cmd(__FILE__);
  cmd.AddValue("phyModeA", "Wifi Phy mode Network A", phyModeA);
  cmd.AddValue("phyModeB", "Wifi Phy mode Network B", phyModeB);
  cmd.AddValue("packetSize", "size of application packet", packetSize);
  cmd.AddValue("numPackets", "number of packets", numPackets);
  cmd.AddValue("interval", "interval between packets", interPacketInterval);
  cmd.AddValue("verbose", "turn on logs", verbose);
  cmd.AddValue("hight", "HAP height (m)", hight);
  cmd.Parse(argc, argv);

  // --- 1. Create Nodes ---
  NodeContainer nodes;
  nodes.Create (7);

  // --- 2. Ground WiFi Configuration ---

  // --- Calculate Atmospheric Loss for HAP-Ground (WiFi) Links ---
  // The signal travels from HAP (20 km) to Earth (0 km). 
  // It passes through the entire thickness of the atmosphere and rain.
  double denseAtmosphereThickness = 20000.0; // Thickness of the dense atmosphere
  
  // 1. Rain loss (from ground to cloud top)
  double rainPathLengthGround = std::min(hight, rainCloudHeight) / 1000.0; 
  double rainLossGround = rainAttenuation * rainPathLengthGround;
  
  // 2. Gas losses (from the ground to the HAP, but limited by the dense atmosphere)
  double gasPathLengthGround = std::min(hight, denseAtmosphereThickness) / 1000.0;
  double oxygenLossGround = oxygenAbsorption * gasPathLengthGround;
  double vaporLossGround = waterVaporAbsorption * gasPathLengthGround;
  
  double totalAtmosphericLossGround = rainLossGround + oxygenLossGround + vaporLossGround;
  
  NS_LOG_UNCOND("\n=== WiFi Ground Link Parameters ===");
  NS_LOG_UNCOND("WiFi TX Pwr: " << Pdbm << " dBm");  
  NS_LOG_UNCOND("TX/RX ant gain: " << antGain << " dBi");  
  NS_LOG_UNCOND("Atmospheric Path Loss Calculations for HAP 1, HAP 2 to Ground WiFi: " << totalAtmosphericLossGround << " dB");  

  WifiHelper wifiA;
  wifiA.SetStandard(WIFI_STANDARD_80211b);
  YansWifiPhyHelper wifiPhyA;
  wifiPhyA.Set("TxGain", DoubleValue(antGain));
  wifiPhyA.Set("RxGain", DoubleValue(antGain));
  wifiPhyA.Set("TxPowerStart", DoubleValue(Pdbm));
  wifiPhyA.Set("TxPowerEnd", DoubleValue(Pdbm));
  YansWifiChannelHelper wifiChannelA;
  wifiChannelA.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannelA.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                       "Exponent", DoubleValue(2.0),
                       "ReferenceDistance", DoubleValue(1.0),
                       "ReferenceLoss", DoubleValue(40.0 + totalAtmosphericLossGround));
  wifiPhyA.SetChannel(wifiChannelA.Create());

  WifiMacHelper wifiMacA;
  wifiA.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue(phyModeA),
                                "ControlMode", StringValue(phyModeA));
  wifiMacA.SetType("ns3::AdhocWifiMac");

  WifiHelper wifiB;
  wifiB.SetStandard(WIFI_STANDARD_80211a);
  YansWifiPhyHelper wifiPhyB;
  wifiPhyB.Set("TxGain", DoubleValue(antGain));
  wifiPhyB.Set("RxGain", DoubleValue(antGain));
  wifiPhyB.Set("TxPowerStart", DoubleValue(Pdbm));
  wifiPhyB.Set("TxPowerEnd", DoubleValue(Pdbm));
  YansWifiChannelHelper wifiChannelB;
  wifiChannelB.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannelB.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                  "Exponent", DoubleValue(2.0),
                                  "ReferenceDistance", DoubleValue(1.0),
                                  "ReferenceLoss", DoubleValue(46.7 + totalAtmosphericLossGround));
  wifiPhyB.SetChannel(wifiChannelB.Create());
  WifiMacHelper wifiMacB;
  wifiB.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue(phyModeB),
                                "ControlMode", StringValue(phyModeB));
  wifiMacB.SetType("ns3::AdhocWifiMac");

  // --- 3. Configure Ka-band Satellite Links (SEPARATED CHANNELS) ---

  WifiHelper wifiSat;
  wifiSat.SetStandard(WIFI_STANDARD_80211a);
  WifiMacHelper wifiMacSat;
  wifiSat.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue("OfdmRate6Mbps"), 
                              "ControlMode", StringValue("OfdmRate6Mbps"),
                              "RtsCtsThreshold", UintegerValue(2200));
  wifiMacSat.SetType("ns3::AdhocWifiMac");

  // --- HAP 1 Links (Freq: 30 GHz Up, 28 GHz Down) ---
  
  // HAP 1 Uplink
  YansWifiChannelHelper wifiChannelSatUp_H1;
  wifiChannelSatUp_H1.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  double refLossH1Up = 20 * log10(freqHap1Up) + 20 * log10(4 * M_PI / 3e8);
  wifiChannelSatUp_H1.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(refLossH1Up));
  
  YansWifiPhyHelper wifiPhySatUp_H1;
  wifiPhySatUp_H1.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySatUp_H1.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySatUp_H1.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySatUp_H1.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  wifiPhySatUp_H1.SetChannel(wifiChannelSatUp_H1.Create());

  // HAP 1 Downlink
  YansWifiChannelHelper wifiChannelSatDown_H1;
  wifiChannelSatDown_H1.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  double refLossH1Down = 20 * log10(freqHap1Down) + 20 * log10(4 * M_PI / 3e8);
  wifiChannelSatDown_H1.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(refLossH1Down));

  YansWifiPhyHelper wifiPhySatDown_H1;
  wifiPhySatDown_H1.Set("TxGain", DoubleValue(satAntGain)); // Sat transmits
  wifiPhySatDown_H1.Set("RxGain", DoubleValue(satAntGain)); 
  wifiPhySatDown_H1.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySatDown_H1.Set("TxPowerEnd", DoubleValue(satTxPower));
  wifiPhySatDown_H1.SetChannel(wifiChannelSatDown_H1.Create());


  // --- HAP 2 Links (Freq: 29 GHz Up, 27 GHz Down) ---
  
  // HAP 2 Uplink
  YansWifiChannelHelper wifiChannelSatUp_H2;
  wifiChannelSatUp_H2.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  double refLossH2Up = 20 * log10(freqHap2Up) + 20 * log10(4 * M_PI / 3e8);
  wifiChannelSatUp_H2.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(refLossH2Up));
  
  YansWifiPhyHelper wifiPhySatUp_H2;
  wifiPhySatUp_H2.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySatUp_H2.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySatUp_H2.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySatUp_H2.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  wifiPhySatUp_H2.SetChannel(wifiChannelSatUp_H2.Create());

  // HAP 2 Downlink
  YansWifiChannelHelper wifiChannelSatDown_H2;
  wifiChannelSatDown_H2.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  double refLossH2Down = 20 * log10(freqHap2Down) + 20 * log10(4 * M_PI / 3e8);
  wifiChannelSatDown_H2.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(refLossH2Down));

  YansWifiPhyHelper wifiPhySatDown_H2;
  wifiPhySatDown_H2.Set("TxGain", DoubleValue(satAntGain)); // Sat transmits
  wifiPhySatDown_H2.Set("RxGain", DoubleValue(satAntGain)); 
  wifiPhySatDown_H2.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySatDown_H2.Set("TxPowerEnd", DoubleValue(satTxPower));
  wifiPhySatDown_H2.SetChannel(wifiChannelSatDown_H2.Create());

  // --- 4. Install NetDevices ---

  // Ground Devices
  NetDeviceContainer wifiDevicesA = wifiA.Install(wifiPhyA, wifiMacA, NodeContainer(nodes.Get(HAP_1), nodes.Get(UT_1_1), nodes.Get(UT_1_2)));
  NetDeviceContainer wifiDevicesB = wifiB.Install(wifiPhyB, wifiMacB, NodeContainer(nodes.Get(HAP_2), nodes.Get(UT_2_1), nodes.Get(UT_2_2)));

  // Satellite Devices Containers
  NetDeviceContainer allSatDevices;
  
  // --- Install HAP 1 Links ---
  NetDeviceContainer hap1UpDev = wifiSat.Install(wifiPhySatUp_H1, wifiMacSat, nodes.Get(HAP_1));
  
  wifiPhySatUp_H1.Set("RxGain", DoubleValue(satAntGain)); 
  wifiPhySatUp_H1.Set("TxGain", DoubleValue(satAntGain)); 
  wifiPhySatUp_H1.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySatUp_H1.Set("TxPowerEnd", DoubleValue(satTxPower));
  NetDeviceContainer satRxDev_H1 = wifiSat.Install(wifiPhySatUp_H1, wifiMacSat, nodes.Get(SATELLITE));
  
  NetDeviceContainer satTxDev_H1 = wifiSat.Install(wifiPhySatDown_H1, wifiMacSat, nodes.Get(SATELLITE));
  
  wifiPhySatDown_H1.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySatDown_H1.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySatDown_H1.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySatDown_H1.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  NetDeviceContainer hap1DownDev = wifiSat.Install(wifiPhySatDown_H1, wifiMacSat, nodes.Get(HAP_1));

  // --- Install HAP 2 Links ---
  NetDeviceContainer hap2UpDev = wifiSat.Install(wifiPhySatUp_H2, wifiMacSat, nodes.Get(HAP_2));
  
  wifiPhySatUp_H2.Set("RxGain", DoubleValue(satAntGain)); 
  wifiPhySatUp_H2.Set("TxGain", DoubleValue(satAntGain));
  wifiPhySatUp_H2.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySatUp_H2.Set("TxPowerEnd", DoubleValue(satTxPower));
  NetDeviceContainer satRxDev_H2 = wifiSat.Install(wifiPhySatUp_H2, wifiMacSat, nodes.Get(SATELLITE));
  
  NetDeviceContainer satTxDev_H2 = wifiSat.Install(wifiPhySatDown_H2, wifiMacSat, nodes.Get(SATELLITE));
  
  wifiPhySatDown_H2.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySatDown_H2.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySatDown_H2.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySatDown_H2.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  NetDeviceContainer hap2DownDev = wifiSat.Install(wifiPhySatDown_H2, wifiMacSat, nodes.Get(HAP_2));

  // Collect all Sat devices
  allSatDevices.Add(hap1UpDev);
  allSatDevices.Add(satRxDev_H1);
  allSatDevices.Add(hap2UpDev);
  allSatDevices.Add(satRxDev_H2);
  allSatDevices.Add(satTxDev_H1);
  allSatDevices.Add(hap1DownDev);
  allSatDevices.Add(satTxDev_H2);
  allSatDevices.Add(hap2DownDev);

  NetDeviceContainer allDevices;
  allDevices.Add(wifiDevicesA);
  allDevices.Add(wifiDevicesB);
  allDevices.Add(allSatDevices);

  // --- 5. Setup Traces ---
  PopulateMacTable(allDevices);
  SetupDeviceTraces(wifiDevicesA);
  SetupDeviceTraces(wifiDevicesB);
  SetupDeviceTraces(allSatDevices);

  // --- 6. Install Internet Stack & IP ---
  InternetStackHelper stack;
  stack.Install (nodes);
  Ipv4AddressHelper address;

  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiA = address.Assign (wifiDevicesA);
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiB = address.Assign (wifiDevicesB);
  
  NetDeviceContainer uplinkNetworkDevices;
  uplinkNetworkDevices.Add(hap1UpDev);
  uplinkNetworkDevices.Add(satRxDev_H1);
  uplinkNetworkDevices.Add(hap2UpDev);
  uplinkNetworkDevices.Add(satRxDev_H2);
  
  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSatUp = address.Assign (uplinkNetworkDevices);
  
  NetDeviceContainer downlinkNetworkDevices;
  downlinkNetworkDevices.Add(satTxDev_H1);
  downlinkNetworkDevices.Add(hap1DownDev);
  downlinkNetworkDevices.Add(satTxDev_H2);
  downlinkNetworkDevices.Add(hap2DownDev);

  address.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSatDown = address.Assign (downlinkNetworkDevices);

  // --- 7. Mobility ---
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
  positionAlloc->Add(Vector(0.0, 0.0, hight));     // HAP 1
  positionAlloc->Add(Vector(-groundDistance/2, 0.0, 0.0));  // UT 1_1
  positionAlloc->Add(Vector(groundDistance/2.0, 0.0, 0.0)); // UT 1_2
  positionAlloc->Add(Vector(groupDistance, 6000.0, hight)); // HAP 2
  positionAlloc->Add(Vector(groupDistance - groundDistance/2, 6000.0, 0.0)); // UT 2_1
  positionAlloc->Add(Vector(groupDistance + groundDistance/2, 6000.0, 0.0)); // UT 2_2
  positionAlloc->Add(Vector(groupDistance/2, 3000.0, satelliteDistance)); // Satellite
  mobility.SetPositionAllocator(positionAlloc);
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(nodes);

  // --- 8. Calculate and Display Ka-band Satellite Link Parameters ---
  Ptr<MobilityModel> hap1Mobility = nodes.Get(HAP_1)->GetObject<MobilityModel>();
  Ptr<MobilityModel> satMobility = nodes.Get(SATELLITE)->GetObject<MobilityModel>();
  Ptr<MobilityModel> hap2Mobility = nodes.Get(HAP_2)->GetObject<MobilityModel>();
  
  double distanceHap1ToSat = hap1Mobility->GetDistanceFrom(satMobility);
  double distanceHap2ToSat = hap2Mobility->GetDistanceFrom(satMobility);
  
  NS_LOG_UNCOND("\n=== Ka-band Satellite Link Parameters ===");
  NS_LOG_UNCOND("Frequency (HAP 1 Downlink): " << freqHap1Down/1e9 << " GHz ");
  NS_LOG_UNCOND("Frequency (HAP 2 Downlink): " << freqHap2Down/1e9 << " GHz ");
  NS_LOG_UNCOND("Frequency (HAP 1 Uplink): " << freqHap1Up/1e9 << " GHz ");
  NS_LOG_UNCOND("Frequency (HAP 2 Uplink): " << freqHap2Up/1e9 << " GHz ");

  NS_LOG_UNCOND("HAP Height: " << hight/1000 << " km");
  NS_LOG_UNCOND("Distance HAP1 to Satellite: " << distanceHap1ToSat/1000 << " km");
  NS_LOG_UNCOND("Distance HAP2 to Satellite: " << distanceHap2ToSat/1000 << " km");
  NS_LOG_UNCOND("Satellite TX Power: " << satTxPower << " dBm");
  NS_LOG_UNCOND("HAP Satellite TX Power: " << hapSatTxPower << " dBm");
  NS_LOG_UNCOND("Satellite Antenna Gain: " << satAntGain << " dBi");
  NS_LOG_UNCOND("HAP Satellite Antenna Gain: " << hapSatAntGain << " dBi");
  
  // Using Downlink freq for budget example (Sat -> HAP)
  double fsplHap1Sat = 20 * log10(distanceHap1ToSat) + 20 * log10(freqHap1Down) + 20 * log10(4 * M_PI / 3e8);
  
  double rainLoss = 0.0; 
  if (hight < rainCloudHeight) {
      rainLoss = rainAttenuation * (rainCloudHeight - hight) / 1000.0;
  }
  double gasPathLength = 0.0;
  if (hight < denseAtmosphereThickness) {
      gasPathLength = (denseAtmosphereThickness - hight) / 1000.0; 
  } else {
      gasPathLength = 0.0;
  }
  double oxygenLoss = oxygenAbsorption * gasPathLength;
  double vaporLoss = waterVaporAbsorption * gasPathLength;
  double totalAtmosphericLoss = rainLoss + oxygenLoss + vaporLoss;
  
  NS_LOG_UNCOND("\nPath Loss Calculations (Sat -> HAP 1, HAP 2):");
  NS_LOG_UNCOND("FSPL: " << fsplHap1Sat << " dB");
  NS_LOG_UNCOND("Rain Loss: " << rainLoss << " dB");
  NS_LOG_UNCOND("Gas Path Length: " << gasPathLength << " km");
  NS_LOG_UNCOND("Total Atmospheric Loss: " << totalAtmosphericLoss << " dB");
  NS_LOG_UNCOND("Total Path Loss: " << (fsplHap1Sat + totalAtmosphericLoss) << " dB");
  
  double eirpSat = satTxPower - 30 + satAntGain; 
  NS_LOG_UNCOND("\nLink Budget (Satellite -> HAP 1, HAP 2):");
  NS_LOG_UNCOND("Satellite EIRP: " << eirpSat << " dBW");
  NS_LOG_UNCOND("Path Loss: " << fsplHap1Sat << " dB");
  NS_LOG_UNCOND("Atmospheric Loss: " << totalAtmosphericLoss << " dB");
  NS_LOG_UNCOND("HAP Antenna Gain: " << hapSatAntGain << " dBi");
  double receivedPower = eirpSat - fsplHap1Sat - totalAtmosphericLoss + hapSatAntGain;
  NS_LOG_UNCOND("Received Power at HAP 1, HAP 2: " << receivedPower << " dBW (" << (receivedPower + 30) << " dBm)");

  // --- 9. Static Routing ---
  Ipv4StaticRoutingHelper staticRoutingHelper;

  // --- UTs Routing ---
  Ptr<Ipv4> ipv4Ut1_1 = nodes.Get(UT_1_1)->GetObject<Ipv4>();
  staticRoutingHelper.GetStaticRouting(ipv4Ut1_1)->SetDefaultRoute(interfacesWifiA.GetAddress(0), ipv4Ut1_1->GetInterfaceForAddress(interfacesWifiA.GetAddress(1)));
  
  Ptr<Ipv4> ipv4Ut2_1 = nodes.Get(UT_2_1)->GetObject<Ipv4>();
  staticRoutingHelper.GetStaticRouting(ipv4Ut2_1)->SetDefaultRoute(interfacesWifiB.GetAddress(0), ipv4Ut2_1->GetInterfaceForAddress(interfacesWifiB.GetAddress(1)));

  // --- HAP 1 Routing ---
  Ptr<Ipv4> ipv4Hap1 = nodes.Get(HAP_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srHap1 = staticRoutingHelper.GetStaticRouting(ipv4Hap1);
  srHap1->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), Ipv4Mask("255.255.255.0"), 
                            interfacesSatUp.GetAddress(1), 
                            ipv4Hap1->GetInterfaceForAddress(interfacesSatUp.GetAddress(0)));

  // --- HAP 2 Routing ---
  Ptr<Ipv4> ipv4Hap2 = nodes.Get(HAP_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srHap2 = staticRoutingHelper.GetStaticRouting(ipv4Hap2);
  srHap2->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 
                            interfacesSatUp.GetAddress(3), 
                            ipv4Hap2->GetInterfaceForAddress(interfacesSatUp.GetAddress(2)));

  // --- Satellite Routing ---
  Ptr<Ipv4> ipv4Sat = nodes.Get(SATELLITE)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srSat = staticRoutingHelper.GetStaticRouting(ipv4Sat);

  srSat->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 
                           interfacesSatDown.GetAddress(1), 
                           ipv4Sat->GetInterfaceForAddress(interfacesSatDown.GetAddress(0)));

  srSat->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), Ipv4Mask("255.255.255.0"), 
                           interfacesSatDown.GetAddress(3), 
                           ipv4Sat->GetInterfaceForAddress(interfacesSatDown.GetAddress(2)));

  // --- 10. Applications ---
  uint16_t port = 9;
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(UT_2_1), tid);
  recvSink->Bind(InetSocketAddress(Ipv4Address::GetAny(), port));
  recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

  Ptr<Socket> source = Socket::CreateSocket(nodes.Get(UT_1_1), tid);
  source->Connect(InetSocketAddress(interfacesWifiB.GetAddress(1), port));

  // --- 11. Flow Monitor Setup ---
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  NS_LOG_UNCOND("\n=== Starting Ka-band Satellite Simulation ===");
  NS_LOG_UNCOND("HAPs are in Stratosphere.");
  
  Simulator::ScheduleWithContext(source->GetNode()->GetId(),
          Seconds(1.0), &GenerateTraffic, source, packetSize, numPackets, interPacketInterval);

  double simTime = 1.0 + (numPackets * interPacketInterval.GetSeconds()) + 5.0;
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // --- 12. Link Level Statistics Output (Flow Based) ---
  std::cout << "\n\n=== Per-Flow Link Loss Statistics (Node-to-Node) ===" << std::endl;
  std::cout << std::left << std::setw(30) << "Flow (Source -> Dest)" 
            << std::right << std::setw(10) << "Tx Pkts" 
            << std::right << std::setw(10) << "Rx Pkts" 
            << std::right << std::setw(10) << "Rx Drop" 
            << std::right << std::setw(10) << "Loss %" << std::endl;
  std::cout << std::string(70, '-') << std::endl;
  
  std::vector<std::pair<uint32_t, uint32_t>> flowKeys;
  for (auto const& [key, stats] : g_flowStats) {
      if (stats.txPackets > 0 || stats.rxPackets > 0 || stats.rxDropped > 0) {
          flowKeys.push_back(key);
      }
  }
  std::sort(flowKeys.begin(), flowKeys.end());

  for (auto const& key : flowKeys) {
      uint32_t srcId = key.first;
      uint32_t dstId = key.second;
      FlowLinkStats stats = g_flowStats[key];
      std::stringstream flowName;
      flowName << GetNodeName(srcId) << " -> " << GetNodeName(dstId);
      double lossRatio = 0.0;
      if (stats.txPackets > 0) lossRatio = ((double)stats.rxDropped / stats.txPackets) * 100.0;
      
      std::cout << std::left << std::setw(30) << flowName.str() 
                << std::right << std::setw(10) << stats.txPackets
                << std::right << std::setw(10) << stats.rxPackets 
                << std::right << std::setw(10) << stats.rxDropped 
                << std::right << std::setw(9) << std::fixed << std::setprecision(1) << lossRatio << "%" 
                << std::endl;
  }
  std::cout << std::string(70, '-') << std::endl;

  // --- 13. End-to-End Flow Monitor Stats ---
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n=== Ka-band Satellite Simulation Results (End-to-End) ===" << std::endl;
  std::cout << "Topology: Ground WiFi <-> HAP (" << hight/1000
   << "km) <-> GEO Sat <-> HAP ("
   << hight/1000 << "km) <-> Ground WiFi" << std::endl;

  // Header of table 
  std::cout << "\n" << std::left << std::setw(5)  << "Flow"
            << std::left << std::setw(28) << "Src (IP [Node])"
            << std::left << std::setw(28) << "Dst (IP [Node])"
            << std::right << std::setw(6)  << "Tx"
            << std::right << std::setw(6)  << "Rx"
            << std::right << std::setw(8)  << "Loss %"
            << std::right << std::setw(10) << "Thrput(Kbps)"
            << std::right << std::setw(9)  << "Del(ms)"
            << std::right << std::setw(9)  << "Jit(ms)" << std::endl;
            
  std::cout << std::string(109, '-') << std::endl;

  for (auto const& [flowId, flowStats] : stats) {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);
      
      std::string srcNodeName = GetNodeNameByIp(t.sourceAddress, nodes);
      std::string dstNodeName = GetNodeNameByIp(t.destinationAddress, nodes);
      
      std::stringstream srcSs, dstSs;
      srcSs << t.sourceAddress << " [" << srcNodeName << "]";
      dstSs << t.destinationAddress << " [" << dstNodeName << "]";

      double lossRatio = 0.0;
      if (flowStats.txPackets > 0) {
          lossRatio = ((double)(flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets) * 100.0;
      }

      double throughput = 0.0;
      double delay = 0.0;
      double jitter = 0.0;
      std::string metricPlaceholder = "-";

      if (flowStats.rxPackets > 0) {
          throughput = (flowStats.rxBytes * 8.0) / (flowStats.timeLastRxPacket.GetSeconds() - flowStats.timeFirstTxPacket.GetSeconds());
          delay = flowStats.delaySum.GetSeconds() / flowStats.rxPackets;
          if (flowStats.rxPackets > 1) {
              jitter = flowStats.jitterSum.GetSeconds() / (flowStats.rxPackets - 1);
          }
          
          std::cout << std::left << std::setw(5) << flowId 
                    << std::left << std::setw(28) << srcSs.str()
                    << std::left << std::setw(28) << dstSs.str()
                    << std::right << std::setw(6) << flowStats.txPackets
                    << std::right << std::setw(6) << flowStats.rxPackets
                    << std::right << std::setw(7) << std::fixed << std::setprecision(1) << lossRatio << "%"
                    << std::right << std::setw(10) << std::fixed << std::setprecision(1) << throughput
                    << std::right << std::setw(9) << std::fixed << std::setprecision(1) << (delay * 1000.0)
                    << std::right << std::setw(9) << std::fixed << std::setprecision(1) << (jitter * 1000.0) 
                    << std::endl;
      } else {
          std::cout << std::left << std::setw(5) << flowId 
                    << std::left << std::setw(28) << srcSs.str()
                    << std::left << std::setw(28) << dstSs.str()
                    << std::right << std::setw(6) << flowStats.txPackets
                    << std::right << std::setw(6) << flowStats.rxPackets
                    << std::right << std::setw(8) << std::fixed << std::setprecision(1) << lossRatio << "%"
                    << std::right << std::setw(10) << metricPlaceholder 
                    << std::right << std::setw(9) << metricPlaceholder 
                    << std::right << std::setw(9) << metricPlaceholder 
                    << std::endl;
      }
  }
  std::cout << std::string(109, '-') << std::endl;

  monitor->SerializeToXmlFile("hap-sat-ka-band-stats.xml", true, true);
  std::cout << "\n=== End of Simulation ===" << std::endl;

  Simulator::Destroy();
  return 0;
}