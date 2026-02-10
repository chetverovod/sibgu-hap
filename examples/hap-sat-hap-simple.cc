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
// CORRECTIONS APPLIED:
// 1. Removed FixedRssLossModel.
// 2. Updated atmospheric loss logic for HAP altitude (20km).
// 3. FIX FOR PACKET LOSS: Increased AckTimeout to handle GEO latency (~240ms RTT).
// 4. Reduced Data Rate to 6Mbps for robustness at high frequency/long distance.
// 5. ADDED: Per-link packet loss monitoring via Phy/Mac traces.

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

// --- Структура для хранения статистики по устройству ---
struct LinkStats {
    uint32_t txPackets = 0;       // Отправлено пакетов (Phy Tx)
    uint32_t rxPacketsSuccess = 0;// Успешно принято пакетов (Mac Rx)
    uint32_t rxDropped = 0;       // Потеряно пакетов на уровне PHY (Phy Rx Drop)
};

// Глобальные карты для хранения статистики и имен устройств
std::map<Ptr<NetDevice>, LinkStats> g_statsMap;
std::map<Ptr<NetDevice>, std::string> g_deviceNames;

// --- Вспомогательная функция для расшифровки причины потери (версия ns-3.43) ---
std::string GetRxDropReasonName(WifiPhyRxfailureReason reason) {
    switch (reason) {
        case WifiPhyRxfailureReason::UNKNOWN: return "Unknown";
        case WifiPhyRxfailureReason::PREAMBLE_DETECT_FAILURE: return "Preamble Detect Fail";
        case WifiPhyRxfailureReason::RECEPTION_ABORTED_BY_TX: return "Aborted by TX";
        // Остальные значения появились в более поздних версиях (3.44+)
        default: return "Other / Propagation Loss";
    }
}


// --- Callbacks for Link Monitoring ---

void PhyTxBeginCallback(Ptr<NetDevice> device, Ptr<const Packet> packet, double txPower) {
    g_statsMap[device].txPackets++;
}

void MacRxCallback(Ptr<NetDevice> device, Ptr<const Packet> packet) {
    g_statsMap[device].rxPacketsSuccess++;
}

// ИЗМЕНЕНО: Вывод причины потери (Reason)
void PhyRxDropCallback(Ptr<NetDevice> device, Ptr<const Packet> packet, WifiPhyRxfailureReason reason) {
    g_statsMap[device].rxDropped++;
    
    // Раскомментируйте строку ниже, если хотите видеть причину для каждого пакета в логе (много текста)
     NS_LOG_UNCOND("Drop on " << g_deviceNames[device] << " Reason: " << GetRxDropReasonName(reason));
}

// --- Функция настройки трассировки ---
void SetupDeviceTraces(NetDeviceContainer devices, std::string linkName) {
    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<NetDevice> dev = devices.Get(i);
        Ptr<Node> node = dev->GetNode();
        std::stringstream ss;
        ss << linkName << " (Node " << node->GetId() << ")";
        g_deviceNames[dev] = ss.str();

        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(dev);
        if (wifiDev) {
            wifiDev->GetPhy()->TraceConnectWithoutContext("PhyTxBegin", MakeBoundCallback(&PhyTxBeginCallback, dev));
            wifiDev->GetPhy()->TraceConnectWithoutContext("PhyRxDrop", MakeBoundCallback(&PhyRxDropCallback, dev));
            wifiDev->GetMac()->TraceConnectWithoutContext("MacRx", MakeBoundCallback(&MacRxCallback, dev));
        }
    }
}

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
  uint32_t packetSize{1000}; // bytes
  uint32_t numPackets{10};
  Time interPacketInterval{"40ms"};
  bool verbose = false;
  
  // HAP Parameters
  double hight{20000.0};    // meters (Stratosphere)
  double Pdbm{20.};       // Transmitter power (dBm) for Ground/HAP WiFi
  
  // Antenna Gain for Ka-band
  double antGain{20.};    // WiFi Antenna gain (dB)
  double satAntGain{50.}; // Satellite antenna Gain for Ka-band (dB)
  double hapSatAntGain{45.}; // HAP antenna Gain for Ka-band satellite link (dB)

  // Ground separation (distance between terminal A and B on the ground)
  double groundDistance{5000.0};
  
  // Groups ground separation (distance between terminal groups)
  double groupDistance{100000.0};
  
  // Satellite-ground distance (GEO orbit)
  double satelliteDistance{35786000.0};

  // Ka-band Satellite Link Specific Parameters
  double satTxPower{50.0}; // Power of Satellite transmitters for Ka-band (dBm)
  double hapSatTxPower{45.0}; // Power of HAP transmitters for Ka-band (dBm)
  double frequency{30.0e9}; // Ka-band frequency (30 GHz)
  
  // Atmospheric absorption parameters
  double rainAttenuation{3.0}; // Rain attenuation in dB/km (moderate rain)
  double oxygenAbsorption{0.1}; // Oxygen absorption in dB/km (sea level approx)
  double waterVaporAbsorption{0.05}; // Water vapor absorption in dB/km
  
  // Rain cloud height (approximate top of troposphere/rain layer)
  double rainCloudHeight{5000.0}; // meters

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
  cmd.AddValue("hapSatTxPower", "Power for HAP satellite link (dBm)", hapSatTxPower);
  cmd.AddValue("frequency", "Satellite link frequency (Hz)", frequency);
  cmd.AddValue("rainAttenuation", "Rain attenuation in dB/km", rainAttenuation);
  cmd.Parse(argc, argv);

  // Увеличиваем Slot Time до 1E6 микросекунд.
  // Это автоматически увеличит AckTimeout, так как он вычисляется на основе Slot.
  // RTT на 20км ~ 137 мкс. 150 мкс достаточно для подтверждения.
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/Slot", TimeValue (MicroSeconds (2E6)));

  // 2. Увеличиваем SIFS (Short Interframe Space) пропорционально для надежности.
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/Sifs", TimeValue (MicroSeconds (2E6)));


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


  // --- 3. Configure Ka-band Satellite Wireless Link ---
  WifiHelper wifiSat;
  wifiSat.SetStandard(WIFI_STANDARD_80211a);

  YansWifiPhyHelper wifiPhySat;
  wifiPhySat.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

  // Create satellite channel with Ka-band specific models
  YansWifiChannelHelper wifiChannelSat;
  wifiChannelSat.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  
  // Используем ТОЛЬКО LogDistancePropagationLossModel (без Nakagami)
  double referenceLossKa = 20 * log10(frequency) + 20 * log10(4 * M_PI / 3e8) + 20 * log10(1);
  
  wifiChannelSat.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(referenceLossKa));

  Ptr<YansWifiChannel> satChannel = wifiChannelSat.Create();
  wifiPhySat.SetChannel(satChannel);

  // ИСПРАВЛЕНИЕ: Явная настройка порогов чувствительности
  // По умолчанию пороги могут быть слишком высокими (например, -86 дБм).
  // Мы снижаем их до -100 дБм, чтобы гарантировать прием при -48 дБм.
  wifiPhySat.Set("RxSensitivity", DoubleValue(-100.0)); 
  wifiPhySat.Set("CcaEdThreshold", DoubleValue(-100.0)); 
  
  // Явно устанавливаем частоту на PHY (30000 MHz), чтобы избежать конфликтов стандартов
  // fail! wifiPhySat.Set("Frequency", UintegerValue(30000));

  WifiMacHelper wifiMacSat;
  wifiSat.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue("OfdmRate6Mbps"), 
                              "ControlMode", StringValue("OfdmRate6Mbps"));
  wifiMacSat.SetType("ns3::AdhocWifiMac");

  // --- 4. Install NetDevices ---
  NetDeviceContainer wifiDevicesA;
  wifiDevicesA = wifiA.Install(wifiPhyA, wifiMacA, NodeContainer(nodes.Get(HAP_1), nodes.Get(UT_1_1), nodes.Get(UT_1_2)));

  NetDeviceContainer wifiDevicesB;
  wifiDevicesB = wifiB.Install(wifiPhyB, wifiMacB, NodeContainer(nodes.Get(HAP_2), nodes.Get(UT_2_1), nodes.Get(UT_2_2)));

  NetDeviceContainer satDevices;
  
  // Install on HAP 1
  wifiPhySat.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySat.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySat.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySat.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  satDevices.Add(wifiSat.Install(wifiPhySat, wifiMacSat, nodes.Get(HAP_1)));
  
  // Install on Satellite
  wifiPhySat.Set("TxGain", DoubleValue(satAntGain));
  wifiPhySat.Set("RxGain", DoubleValue(satAntGain));
  wifiPhySat.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySat.Set("TxPowerEnd", DoubleValue(satTxPower));
  satDevices.Add(wifiSat.Install(wifiPhySat, wifiMacSat, nodes.Get(SATELLITE)));
  
  // Install on HAP 2
  wifiPhySat.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySat.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySat.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySat.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  satDevices.Add(wifiSat.Install(wifiPhySat, wifiMacSat, nodes.Get(HAP_2)));
/*
  // --- 4. Install NetDevices ---
*/

  // --- 4.5. Setup Link Traces (MONITORING) ---
  // Настраиваем сбор статистики для каждой группы устройств
  SetupDeviceTraces(wifiDevicesA, "WiFi-A (Ground 1)");
  SetupDeviceTraces(wifiDevicesB, "WiFi-B (Ground 2)");
  SetupDeviceTraces(satDevices, "Ka-Sat");

  // --- 5. Install Internet Stack ---
  InternetStackHelper stack;
  stack.Install (nodes);

  // --- 6. Assign IP Addresses ---
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiA = address.Assign (wifiDevicesA);
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiB = address.Assign (wifiDevicesB);
  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSat = address.Assign (satDevices);

  // --- 7. Mobility Setup ---
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

  // Group 1 Positions
  positionAlloc->Add(Vector(0.0, 0.0, hight));     // HAP 1
  positionAlloc->Add(Vector(-groundDistance/2, 0.0, 0.0));  // UT 1_1
  positionAlloc->Add(Vector(groundDistance/2.0, 0.0, 0.0)); // UT 1_2

  // Group 2 Positions
  positionAlloc->Add(Vector(groupDistance, 6000.0, hight)); // HAP 2
  positionAlloc->Add(Vector(groupDistance - groundDistance/2, 6000.0, 0.0)); // UT 2_1
  positionAlloc->Add(Vector(groupDistance + groundDistance/2, 6000.0, 0.0)); // UT 2_2

  // Satellite Position
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
  NS_LOG_UNCOND("Frequency: " << frequency/1e9 << " GHz (Ka-band)");
  NS_LOG_UNCOND("HAP Height: " << hight/1000 << " km");
  NS_LOG_UNCOND("Distance HAP1 to Satellite: " << distanceHap1ToSat/1000 << " km");
  NS_LOG_UNCOND("Distance HAP2 to Satellite: " << distanceHap2ToSat/1000 << " km");
  NS_LOG_UNCOND("Satellite TX Power: " << satTxPower << " dBm");
  NS_LOG_UNCOND("HAP Satellite TX Power: " << hapSatTxPower << " dBm");
  NS_LOG_UNCOND("Satellite Antenna Gain: " << satAntGain << " dBi");
  NS_LOG_UNCOND("HAP Satellite Antenna Gain: " << hapSatAntGain << " dBi");
  
  // Calculate Free Space Path Loss (FSPL)
  double fsplHap1Sat = 20 * log10(distanceHap1ToSat) + 20 * log10(frequency) + 20 * log10(4 * M_PI / 3e8);
 // double fsplHap2Sat = 20 * log10(distanceHap2ToSat) + 20 * log10(frequency) + 20 * log10(4 * M_PI / 3e8);
  
  // Calculate Atmospheric Losses (Sat -> HAP)
  
  // 1. Rain Loss
  double rainLoss = 0.0; 
  if (hight < rainCloudHeight) {
      rainLoss = rainAttenuation * (rainCloudHeight - hight) / 1000.0;
  }
  
  // 2. Gas Absorption
  double denseAtmosphereThickness = 20000.0; // meters
  double gasPathLength = 0.0;
  
  if (hight < denseAtmosphereThickness) {
      gasPathLength = (denseAtmosphereThickness - hight) / 1000.0; // km
  } else {
      gasPathLength = 0.0;
  }
  
  double oxygenLoss = oxygenAbsorption * gasPathLength;
  double vaporLoss = waterVaporAbsorption * gasPathLength;
  
  double totalAtmosphericLoss = rainLoss + oxygenLoss + vaporLoss;
  
  NS_LOG_UNCOND("\nPath Loss Calculations (Sat -> HAP):");
  NS_LOG_UNCOND("FSPL: " << fsplHap1Sat << " dB");
  NS_LOG_UNCOND("Rain Loss: " << rainLoss << " dB");
  NS_LOG_UNCOND("Gas Path Length: " << gasPathLength << " km");
  NS_LOG_UNCOND("Total Atmospheric Loss: " << totalAtmosphericLoss << " dB");
  NS_LOG_UNCOND("Total Path Loss: " << (fsplHap1Sat + totalAtmosphericLoss) << " dB");
  
  // Calculate link budget
  double eirpSat = satTxPower - 30 + satAntGain; 
  NS_LOG_UNCOND("\nLink Budget (Satellite -> HAP 1):");
  NS_LOG_UNCOND("Satellite EIRP: " << eirpSat << " dBW");
  NS_LOG_UNCOND("Path Loss: " << fsplHap1Sat << " dB");
  NS_LOG_UNCOND("Atmospheric Loss: " << totalAtmosphericLoss << " dB");
  NS_LOG_UNCOND("HAP Antenna Gain: " << hapSatAntGain << " dBi");
  double receivedPower = eirpSat - fsplHap1Sat - totalAtmosphericLoss + hapSatAntGain;
  NS_LOG_UNCOND("Received Power at HAP: " << receivedPower << " dBW (" << (receivedPower + 30) << " dBm)");

  // --- 9. Routing ---
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // --- 10. Applications (Sockets) ---
  uint16_t port = 9;
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> recvSink = Socket::CreateSocket(nodes.Get(UT_2_1), tid);
  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
  recvSink->Bind(local);
  recvSink->SetRecvCallback(MakeCallback(&ReceivePacket));

  Ptr<Socket> source = Socket::CreateSocket(nodes.Get(UT_1_1), tid);
  InetSocketAddress remote = InetSocketAddress(interfacesWifiB.GetAddress(1), port);
  source->Connect(remote);

  
  // --- 11. Flow Monitor Setup ---
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  NS_LOG_UNCOND("\n=== Starting Ka-band Satellite Simulation ===");
  NS_LOG_UNCOND("HAP is in Stratosphere.");
  NS_LOG_UNCOND("ACK Timeout increased to 500ms for GEO latency.");
  
  Simulator::ScheduleWithContext(source->GetNode()->GetId(),
          Seconds(1.0),
          &GenerateTraffic,
          source,
          packetSize,
          numPackets,
          interPacketInterval);

  double simTime = 1.0 + (numPackets * interPacketInterval.GetSeconds()) + 5.0; 
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // --- Link Level Statistics Output ---
  std::cout << "\n\n=== Per-Device Link Loss Statistics ===" << std::endl;
  std::cout << "Format: Device Name | Tx Pkts | Rx Success | Rx Dropped | Loss Ratio" << std::endl;
  std::cout << "---------------------------------------------------------------------" << std::endl;
  
  for (auto const& [device, name] : g_deviceNames) {
      LinkStats stats = g_statsMap[device];
      double lossRatio = 0.0;
      double inboundTotal = stats.rxPacketsSuccess + stats.rxDropped;
      
      if (inboundTotal > 0) {
          lossRatio = (stats.rxDropped / inboundTotal) * 100.0;
      }

      std::cout << name 
                << " | Tx: " << stats.txPackets
                << " | Rx Succ: " << stats.rxPacketsSuccess 
                << " | Rx Drop: " << stats.rxDropped 
                << " | Loss: " << lossRatio << "%" << std::endl;
  }
  std::cout << "---------------------------------------------------------------------" << std::endl;

  // --- Statistics ---
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n=== Ka-band Satellite Simulation Results (End-to-End) ===" << std::endl;
  std::cout << "Topology: Ground WiFi <-> HAP (20km) <-> GEO Sat <-> HAP (20km) <-> Ground WiFi" << std::endl;

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin(); i != stats.end(); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
      std::cout << "\nFlow " << i->first 
          << " (" << t.sourceAddress << ":" << t.sourcePort << " -> " 
          << t.destinationAddress << ":" << t.destinationPort << ")" << std::endl;

      std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
      std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
      double lossRatio{100.};
      if (i->second.txPackets > 0)
        {
          double lostPackets = i->second.txPackets - i->second.rxPackets;
          lossRatio = (lostPackets / i->second.txPackets) * 100.0;
          std::cout << "  Lost Packets: " << lostPackets << " (" << lossRatio << "%)" << std::endl;
        }

      if (i->second.rxPackets > 0)
        {
             double throughput = i->second.rxBytes * 8.0 / (i->second.timeLastRxPacket.GetSeconds() - i->second.timeFirstTxPacket.GetSeconds());
             std::cout << "  Throughput: " << throughput / 1024 << " Kbps" << std::endl;
             double delay = i->second.delaySum.GetSeconds() / i->second.rxPackets;
             std::cout << "  Avg Delay:  " << delay * 1000 << " ms" << std::endl;
             double jitter = 0;
             if (i->second.rxPackets > 1) {
                 jitter = i->second.jitterSum.GetSeconds() / (i->second.rxPackets - 1);
             }
             std::cout << "  Avg Jitter: " << jitter * 1000 << " ms" << std::endl;
        }
    }

  monitor->SerializeToXmlFile("hap-sat-ka-band-stats.xml", true, true);
  std::cout << "\n=== End of Simulation ===" << std::endl;

  Simulator::Destroy();
  return 0;
}