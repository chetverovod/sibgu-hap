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
// 6. UPDATED: Flow-based statistics to see exact Source -> Destination loss.
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

// Для работы с заголовками WiFi
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

// --- Глобальная карта для сопоставления MAC-адреса и ID узла ---
std::map<Mac48Address, uint32_t> g_macToNodeId;

// --- Структура для хранения статистики потока (Source -> Destination) ---
struct FlowLinkStats {
    uint32_t txPackets = 0;
    uint32_t rxPackets = 0;
    uint32_t rxDropped = 0;
};

// --- Глобальная карта статистики: Key = pair(SrcNodeId, DstNodeId) ---
std::map<std::pair<uint32_t, uint32_t>, FlowLinkStats> g_flowStats;

// --- Вспомогательная функция для получения имени узла по его ID ---
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

// --- Функция для заполнения карты соответствия MAC-адресов Node ID ---
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
    
    // Игнорируем широковещательные и групповые пакеты для чистоты таблицы
    if (destAddr.IsGroup()) return; 

    // Находим ID узла-получателя
    auto it = g_macToNodeId.find(destAddr);
    if (it != g_macToNodeId.end()) {
        uint32_t srcId = device->GetNode()->GetId();
        uint32_t dstId = it->second;
        
        // Увеличиваем счетчик TX для потока Src -> Dst
        g_flowStats[std::make_pair(srcId, dstId)].txPackets++;
    }
}

void MacRxCallback(Ptr<NetDevice> device, Ptr<const Packet> packet) {
    WifiMacHeader header;
    packet->PeekHeader(header);
    
    Mac48Address srcAddr = header.GetAddr2();
    
    // Находим ID узла-отправителя
    auto it = g_macToNodeId.find(srcAddr);
    if (it != g_macToNodeId.end()) {
        uint32_t srcId = it->second;
        uint32_t dstId = device->GetNode()->GetId();
        
        // Увеличиваем счетчик RX для потока Src -> Dst
        g_flowStats[std::make_pair(srcId, dstId)].rxPackets++;
    }
}

void PhyRxDropCallback(Ptr<NetDevice> device, Ptr<const Packet> packet, WifiPhyRxfailureReason reason) {
    WifiMacHeader header;
    // Пытаемся прочитать заголовок. При сбое PHY он может быть поврежден, но PeekHeader обычно работает
    if (packet->PeekHeader(header)) {
        Mac48Address srcAddr = header.GetAddr2();
        
        auto it = g_macToNodeId.find(srcAddr);
        if (it != g_macToNodeId.end()) {
            uint32_t srcId = it->second;
            uint32_t dstId = device->GetNode()->GetId();
            
            // Увеличиваем счетчик Drop для потока Src -> Dst
            g_flowStats[std::make_pair(srcId, dstId)].rxDropped++;
        }
    }
}

// --- Функция настройки трассировки ---
void SetupDeviceTraces(NetDeviceContainer devices) {
    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<NetDevice> dev = devices.Get(i);
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

// --- Вспомогательная функция для получения имени узла по IP-адресу (для FlowMonitor) ---
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
  // --- ОТКЛЮЧЕНИЕ РЕТРАНСЛЯЦИЙ (NO RETRIES) ---
  // Настраиваем количество попыток отправки равным 0.
  // Устройство отправит пакет 1 раз. Если ACK не придет (а он не придет из-за GEO задержки),
  // пакет будет считаться потерянным, но повторных попыток не будет.
  
// --- General Parameters ---
  std::string phyModeA("DsssRate1Mbps"); // 802.11b
  std::string phyModeB("OfdmRate6Mbps"); // 802.11a
  uint32_t packetSize{1500}; // bytes
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
                                   "m0", DoubleValue(10.0), 
                                   "m1", DoubleValue(10.0),
                                   "m2", DoubleValue(10.0));
  
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
                                   "m0", DoubleValue(10.0), 
                                   "m1", DoubleValue(10.0),
                                   "m2", DoubleValue(10.0));

  wifiPhyB.SetChannel(wifiChannelB.Create());

  WifiMacHelper wifiMacB;
  wifiB.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue(phyModeB),
                              "ControlMode", StringValue(phyModeB));
  wifiMacB.SetType("ns3::AdhocWifiMac");

  // --- 3. Configure Ka-band Satellite Wireless Links (Final Stable Config) ---

  // Общие настройки Wifi для спутника
  WifiHelper wifiSat;
  wifiSat.SetStandard(WIFI_STANDARD_80211a);
  
  WifiMacHelper wifiMacSat;
  wifiSat.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue("OfdmRate6Mbps"), 
                              "ControlMode", StringValue("OfdmRate6Mbps"),
                              "RtsCtsThreshold", UintegerValue(2200));

  wifiMacSat.SetType("ns3::AdhocWifiMac");

  // --- UPLINK CHANNEL (HAP -> Satellite) ---
  YansWifiPhyHelper wifiPhySatUp;
  wifiPhySatUp.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
/*
wifiPhySatUp.Set("Sifs", TimeValue(MicroSeconds(16))); 
  wifiPhySatUp.Set("Slot", TimeValue(MilliSeconds(300))); 
*/
  YansWifiChannelHelper wifiChannelSatUp;
  wifiChannelSatUp.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  double referenceLossKa = 20 * log10(frequency) + 20 * log10(4 * M_PI / 3e8) + 20 * log10(1);
  wifiChannelSatUp.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(referenceLossKa));
  wifiPhySatUp.SetChannel(wifiChannelSatUp.Create());

  // --- DOWNLINK CHANNEL (Satellite -> HAP) ---
  YansWifiPhyHelper wifiPhySatDown;
  wifiPhySatDown.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
  /*
  wifiPhySatDown.Set("Sifs", TimeValue(MicroSeconds(16)));
  wifiPhySatDown.Set("Slot", TimeValue(MilliSeconds(300)));
*/
  YansWifiChannelHelper wifiChannelSatDown;
  wifiChannelSatDown.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannelSatDown.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                   "Exponent", DoubleValue(2.0),
                                   "ReferenceDistance", DoubleValue(1.0),
                                   "ReferenceLoss", DoubleValue(referenceLossKa));
  wifiPhySatDown.SetChannel(wifiChannelSatDown.Create());

  // --- 4. Install NetDevices with Bidirectional Links ---
  
  // Установка наземных устройств
  NetDeviceContainer wifiDevicesA;
  wifiDevicesA = wifiA.Install(wifiPhyA, wifiMacA, NodeContainer(nodes.Get(HAP_1), nodes.Get(UT_1_1), nodes.Get(UT_1_2)));

  NetDeviceContainer wifiDevicesB;
  wifiDevicesB = wifiB.Install(wifiPhyB, wifiMacB, NodeContainer(nodes.Get(HAP_2), nodes.Get(UT_2_1), nodes.Get(UT_2_2)));

  // Контейнеры
  NetDeviceContainer satDevicesUp;    
  NetDeviceContainer satDevicesDown;  
  NetDeviceContainer hapDevicesUp;    
  NetDeviceContainer hapDevicesDown;  

  // --- Настройка Uplink ---
  wifiPhySatUp.Set("TxGain", DoubleValue(hapSatAntGain));
  wifiPhySatUp.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySatUp.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySatUp.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  hapDevicesUp.Add(wifiSat.Install(wifiPhySatUp, wifiMacSat, nodes.Get(HAP_1)));
  hapDevicesUp.Add(wifiSat.Install(wifiPhySatUp, wifiMacSat, nodes.Get(HAP_2)));

  wifiPhySatUp.Set("TxGain", DoubleValue(satAntGain)); 
  wifiPhySatUp.Set("RxGain", DoubleValue(satAntGain));
  wifiPhySatUp.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySatUp.Set("TxPowerEnd", DoubleValue(satTxPower));
  satDevicesUp.Add(wifiSat.Install(wifiPhySatUp, wifiMacSat, nodes.Get(SATELLITE)));

  // --- Настройка Downlink ---
  wifiPhySatDown.Set("TxGain", DoubleValue(satAntGain));
  wifiPhySatDown.Set("RxGain", DoubleValue(satAntGain)); 
  wifiPhySatDown.Set("TxPowerStart", DoubleValue(satTxPower));
  wifiPhySatDown.Set("TxPowerEnd", DoubleValue(satTxPower));
  satDevicesDown.Add(wifiSat.Install(wifiPhySatDown, wifiMacSat, nodes.Get(SATELLITE)));

  wifiPhySatDown.Set("TxGain", DoubleValue(hapSatAntGain)); 
  wifiPhySatDown.Set("RxGain", DoubleValue(hapSatAntGain));
  wifiPhySatDown.Set("TxPowerStart", DoubleValue(hapSatTxPower));
  wifiPhySatDown.Set("TxPowerEnd", DoubleValue(hapSatTxPower));
  hapDevicesDown.Add(wifiSat.Install(wifiPhySatDown, wifiMacSat, nodes.Get(HAP_1)));
  hapDevicesDown.Add(wifiSat.Install(wifiPhySatDown, wifiMacSat, nodes.Get(HAP_2)));

  // Объединяем устройства
  NetDeviceContainer allSatDevices;
  allSatDevices.Add(satDevicesUp);
  allSatDevices.Add(satDevicesDown);
  allSatDevices.Add(hapDevicesUp);
  allSatDevices.Add(hapDevicesDown);
  
  NetDeviceContainer allDevices;
  allDevices.Add(wifiDevicesA);
  allDevices.Add(wifiDevicesB);
  allDevices.Add(allSatDevices);

  // --- 4.5. Setup Link Traces ---
  
  // 1. Заполняем таблицу MAC-адресов
  PopulateMacTable(allDevices);
  
  // 2. Подключаем трассировщики
  SetupDeviceTraces(wifiDevicesA);
  SetupDeviceTraces(wifiDevicesB);
  SetupDeviceTraces(allSatDevices);

// после установки всех устройств и перед SetupDeviceTraces:
NS_LOG_UNCOND("\n=== Checking Satellite Device Configuration ===");

if (satDevicesUp.GetN() > 0) {
    Ptr<WifiNetDevice> satDev = DynamicCast<WifiNetDevice>(satDevicesUp.Get(0));
    if (satDev) {
        Ptr<WifiPhy> phy = satDev->GetPhy();
        Ptr<WifiMac> mac = satDev->GetMac();
        
        NS_LOG_UNCOND("Satellite Uplink Info:");
        NS_LOG_UNCOND("  - MAC Type: " << mac->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - SIFS: " << phy->GetSifs().GetMicroSeconds() << " μs");
        NS_LOG_UNCOND("  - Slot: " << phy->GetSlot().GetMicroSeconds() << " μs");
    }
}

if (satDevicesDown.GetN() > 0) {
    Ptr<WifiNetDevice> satDev = DynamicCast<WifiNetDevice>(satDevicesDown.Get(0));
    if (satDev) {
        Ptr<WifiPhy> phy = satDev->GetPhy();
        Ptr<WifiMac> mac = satDev->GetMac();
        
        NS_LOG_UNCOND("Satellite Downlink Info:");
        NS_LOG_UNCOND("  - MAC Type: " << mac->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - SIFS: " << phy->GetSifs().GetMicroSeconds() << " μs");
        NS_LOG_UNCOND("  - Slot: " << phy->GetSlot().GetMicroSeconds() << " μs");
    }
}

// Проверяем конфигурацию первого HAP спутникового устройства
if (hapDevicesUp.GetN() > 0) {
    Ptr<WifiNetDevice> satDev = DynamicCast<WifiNetDevice>(hapDevicesUp.Get(0));
    if (satDev) {
        Ptr<WifiPhy> phy = satDev->GetPhy();
        Ptr<WifiMac> mac = satDev->GetMac();
        
        NS_LOG_UNCOND("HAP Satellite Device Uplink Info:");
        NS_LOG_UNCOND("  - MAC Type: " << mac->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - PHY Standard: 802.11a");
        NS_LOG_UNCOND("  - SIFS: " << phy->GetSifs().GetMicroSeconds() << " μs");
        NS_LOG_UNCOND("  - Slot: " << phy->GetSlot().GetMicroSeconds() << " μs");
    }
}

if (hapDevicesDown.GetN() > 0) {
    Ptr<WifiNetDevice> satDev = DynamicCast<WifiNetDevice>(hapDevicesDown.Get(0));
    if (satDev) {
        Ptr<WifiPhy> phy = satDev->GetPhy();
        Ptr<WifiMac> mac = satDev->GetMac();
        
        NS_LOG_UNCOND("HAP Satellite Device Downlink Info:");
        NS_LOG_UNCOND("  - MAC Type: " << mac->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - PHY Standard: 802.11a");
        NS_LOG_UNCOND("  - SIFS: " << phy->GetSifs().GetMicroSeconds() << " μs");
        NS_LOG_UNCOND("  - Slot: " << phy->GetSlot().GetMicroSeconds() << " μs");
    }
}

// Аналогично для наземных устройств
if (wifiDevicesA.GetN() > 0) {
    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(wifiDevicesA.Get(0));
    if (wifiDev) {
        Ptr<WifiPhy> phy = wifiDev->GetPhy();
        Ptr<WifiMac> mac = wifiDev->GetMac();
        phy->SetAttribute("Slot", TimeValue(MicroSeconds(70)));
        NS_LOG_UNCOND("Group 1 WiFi Device Info:");
        NS_LOG_UNCOND("  - MAC Type: " << wifiDev->GetMac()->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - PHY Standard: 802.11b");
        NS_LOG_UNCOND("  - Data Mode: " << phyModeA);
        NS_LOG_UNCOND("  - SIFS: " << phy->GetSifs().GetMicroSeconds() << " μs");
        NS_LOG_UNCOND("  - Slot: " << phy->GetSlot().GetMicroSeconds() << " μs");
    }
}

if (wifiDevicesB.GetN() > 0) {
    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(wifiDevicesB.Get(0));
    if (wifiDev) {
        Ptr<WifiPhy> phy = wifiDev->GetPhy();
        Ptr<WifiMac> mac = wifiDev->GetMac();
        phy->SetAttribute("Slot", TimeValue(MicroSeconds(70)));
        NS_LOG_UNCOND("Group 2 WiFi Device Info:");
        NS_LOG_UNCOND("  - MAC Type: " << wifiDev->GetMac()->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - PHY Standard: 802.11b");
        NS_LOG_UNCOND("  - Data Mode: " << phyModeB);
        NS_LOG_UNCOND("  - SIFS: " << phy->GetSifs().GetMicroSeconds() << " μs");
        NS_LOG_UNCOND("  - Slot: " << phy->GetSlot().GetMicroSeconds() << " μs");
    }
}    


  // --- 5. Install Internet Stack ---
  InternetStackHelper stack;
  stack.Install (nodes);
  
   // --- 6. Assign IP Addresses ---
  Ipv4AddressHelper address;
  
  // Наземные сети
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiA = address.Assign (wifiDevicesA);
  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesWifiB = address.Assign (wifiDevicesB);
  
  // Спутниковые сети
  
  // --- UPLINK NETWORK (10.1.3.0) ---
  NetDeviceContainer uplinkNetworkDevices;
  uplinkNetworkDevices.Add(hapDevicesUp); // HAP 1 & 2
  uplinkNetworkDevices.Add(satDevicesUp); // Satellite
  
  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSatUp = address.Assign (uplinkNetworkDevices); 
  
  // --- DOWNLINK NETWORK (10.1.4.0) ---
  NetDeviceContainer downlinkNetworkDevices;
  downlinkNetworkDevices.Add(satDevicesDown); // Satellite
  downlinkNetworkDevices.Add(hapDevicesDown); // HAP 1 & 2
  
  address.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSatDown = address.Assign (downlinkNetworkDevices);  

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
  
  double fsplHap1Sat = 20 * log10(distanceHap1ToSat) + 20 * log10(frequency) + 20 * log10(4 * M_PI / 3e8);
  double rainLoss = 0.0; 
  if (hight < rainCloudHeight) {
      rainLoss = rainAttenuation * (rainCloudHeight - hight) / 1000.0;
  }
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
  
  double eirpSat = satTxPower - 30 + satAntGain; 
  NS_LOG_UNCOND("\nLink Budget (Satellite -> HAP 1):");
  NS_LOG_UNCOND("Satellite EIRP: " << eirpSat << " dBW");
  NS_LOG_UNCOND("Path Loss: " << fsplHap1Sat << " dB");
  NS_LOG_UNCOND("Atmospheric Loss: " << totalAtmosphericLoss << " dB");
  NS_LOG_UNCOND("HAP Antenna Gain: " << hapSatAntGain << " dBi");
  double receivedPower = eirpSat - fsplHap1Sat - totalAtmosphericLoss + hapSatAntGain;
  NS_LOG_UNCOND("Received Power at HAP: " << receivedPower << " dBW (" << (receivedPower + 30) << " dBm)");

  // --- 9. Routing (Static Routing) ---
  Ipv4StaticRoutingHelper staticRoutingHelper;

  // --- Маршрутизация для наземных терминалов (UTs) ---
  Ptr<Ipv4> ipv4Ut1_1 = nodes.Get(UT_1_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt1_1 = staticRoutingHelper.GetStaticRouting(ipv4Ut1_1);
  srUt1_1->SetDefaultRoute(interfacesWifiA.GetAddress(0), ipv4Ut1_1->GetInterfaceForAddress(interfacesWifiA.GetAddress(1)));

  Ptr<Ipv4> ipv4Ut1_2 = nodes.Get(UT_1_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt1_2 = staticRoutingHelper.GetStaticRouting(ipv4Ut1_2);
  srUt1_2->SetDefaultRoute(interfacesWifiA.GetAddress(0), ipv4Ut1_2->GetInterfaceForAddress(interfacesWifiA.GetAddress(2)));

  Ptr<Ipv4> ipv4Ut2_1 = nodes.Get(UT_2_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt2_1 = staticRoutingHelper.GetStaticRouting(ipv4Ut2_1);
  srUt2_1->SetDefaultRoute(interfacesWifiB.GetAddress(0), ipv4Ut2_1->GetInterfaceForAddress(interfacesWifiB.GetAddress(1)));

  Ptr<Ipv4> ipv4Ut2_2 = nodes.Get(UT_2_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt2_2 = staticRoutingHelper.GetStaticRouting(ipv4Ut2_2);
  srUt2_2->SetDefaultRoute(interfacesWifiB.GetAddress(0), ipv4Ut2_2->GetInterfaceForAddress(interfacesWifiB.GetAddress(2)));

  // --- Маршрутизация для HAP 1 ---
  Ptr<Ipv4> ipv4Hap1 = nodes.Get(HAP_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srHap1 = staticRoutingHelper.GetStaticRouting(ipv4Hap1);
  srHap1->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), Ipv4Mask("255.255.255.0"), 
                            interfacesSatUp.GetAddress(2), 
                            ipv4Hap1->GetInterfaceForAddress(interfacesSatUp.GetAddress(0)));

  // --- Маршрутизация для HAP 2 ---
  Ptr<Ipv4> ipv4Hap2 = nodes.Get(HAP_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srHap2 = staticRoutingHelper.GetStaticRouting(ipv4Hap2);
  srHap2->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 
                            interfacesSatDown.GetAddress(0), 
                            ipv4Hap2->GetInterfaceForAddress(interfacesSatDown.GetAddress(2)));

  // --- Маршрутизация для Спутника ---
  Ptr<Ipv4> ipv4Sat = nodes.Get(SATELLITE)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srSat = staticRoutingHelper.GetStaticRouting(ipv4Sat);

  srSat->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 
                           interfacesSatDown.GetAddress(1), 
                           ipv4Sat->GetInterfaceForAddress(interfacesSatDown.GetAddress(0)));

  srSat->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), Ipv4Mask("255.255.255.0"), 
                           interfacesSatUp.GetAddress(1), 
                           ipv4Sat->GetInterfaceForAddress(interfacesSatUp.GetAddress(2)));


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

  // --- Link Level Statistics Output (Flow Based) ---
  std::cout << "\n\n=== Per-Flow Link Loss Statistics (Node-to-Node) ===" << std::endl;
  
  // Заголовок таблицы
  std::cout << std::left << std::setw(30) << "Flow (Source -> Dest)" 
            << std::right << std::setw(10) << "Tx Pkts" 
            << std::right << std::setw(10) << "Rx Pkts" 
            << std::right << std::setw(10) << "Rx Drop" 
            << std::right << std::setw(10) << "Loss %" << std::endl;
            
  std::cout << std::string(70, '-') << std::endl;
  
  // Создаем вектор для сортировки потоков
  std::vector<std::pair<uint32_t, uint32_t>> flowKeys;
  for (auto const& [key, stats] : g_flowStats) {
      if (stats.txPackets > 0 || stats.rxPackets > 0 || stats.rxDropped > 0) {
          flowKeys.push_back(key);
      }
  }
  
  // Сортируем: сначала по Source ID, потом по Dest ID
  std::sort(flowKeys.begin(), flowKeys.end());

  for (auto const& key : flowKeys) {
      uint32_t srcId = key.first;
      uint32_t dstId = key.second;
      FlowLinkStats stats = g_flowStats[key];
      
      std::stringstream flowName;
      flowName << GetNodeName(srcId) << " -> " << GetNodeName(dstId);
      
      double lossRatio = 0.0;
      if (stats.txPackets > 0) {
          lossRatio = ((double)stats.rxDropped / stats.txPackets) * 100.0;
      }
      
      std::cout << std::left << std::setw(30) << flowName.str() 
                << std::right << std::setw(10) << stats.txPackets
                << std::right << std::setw(10) << stats.rxPackets 
                << std::right << std::setw(10) << stats.rxDropped 
                << std::right << std::setw(9) << std::fixed << std::setprecision(1) << lossRatio << "%" 
                << std::endl;
  }
  std::cout << std::string(70, '-') << std::endl;

  // --- Statistics ---
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n=== Ka-band Satellite Simulation Results (End-to-End) ===" << std::endl;
  std::cout << "Topology: Ground WiFi <-> HAP (" << hight/1000
   << "km) <-> GEO Sat <-> HAP ("
   << hight/1000 << "km) <-> Ground WiFi" << std::endl;

  // Заголовок таблицы 
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

  // Вывод данных по каждому потоку
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