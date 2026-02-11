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
    std::map<std::string, uint32_t> dropReasons;
};

// Глобальные карты для хранения статистики и имен устройств
std::map<Ptr<NetDevice>, LinkStats> g_statsMap;
std::map<Ptr<NetDevice>, std::string> g_deviceNames;

// --- Вспомогательная функция для получения имени узла по его ID ---
std::string GetNodeName(uint32_t id) {
    switch (id) {
        case HAP_1:   return "HAP_1";
        case UT_1_1:  return "UT_1_1";
        case UT_1_2:  return "UT_1_2";
        case HAP_2:   return "HAP_2";
        case UT_2_1:  return "UT_2_1";
        case UT_2_2:  return "UT_2_2";
        case SATELLITE: return "SATELLITE";
        default:      return "Unknown Node";
    }
}

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
    // NS_LOG_UNCOND("Drop on " << g_deviceNames[device] << " Reason: " << GetRxDropReasonName(reason));
    std::string reasonName = GetRxDropReasonName(reason);
    g_statsMap[device].dropReasons[reasonName]++;
}

// --- Функция настройки трассировки ---
void SetupDeviceTraces(NetDeviceContainer devices, std::string linkName) {
    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<NetDevice> dev = devices.Get(i);
        Ptr<Node> node = dev->GetNode();
        std::stringstream ss;
        // <-- НОВАЯ СТРОКА (используем имя узла) -->
        ss << linkName << " [Node: " << GetNodeName(node->GetId()) << "]";
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

// --- Вспомогательная функция для получения имени узла по IP-адресу ---
std::string GetNodeNameByIp(Ipv4Address ip, NodeContainer nodes) {
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<Node> node = nodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (ipv4) {
            // Проходим по всем интерфейсам узла
            for (uint32_t j = 0; j < ipv4->GetNInterfaces(); ++j) {
                // Проходим по всем адресам на интерфейсе
                for (uint32_t k = 0; k < ipv4->GetNAddresses(j); ++k) {
                    if (ipv4->GetAddress(j, k).GetLocal() == ip) {
                        // Нашли совпадение IP, возвращаем имя узла по его ID
                        return GetNodeName(i);
                    }
                }
            }
        }
    }
    return "Unknown Node";
}

int 
main (int argc, char *argv[])
{
  //Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/AckTimeout", TimeValue (MicroSeconds (100)));
  Config::Set ("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/AdhocWifiMac/AckTimeout", TimeValue (MilliSeconds (300)));

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
  
  // Настраиваем RemoteStationManager.
  // Отключаем RTS/CTS для ускорения (пакет 1500 байт).
  // НЕ настраиваем MinCw/MaxCw/AckTimeout, так как они вызывают ошибки компиляции/запуска.
  wifiSat.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                              "DataMode", StringValue("OfdmRate6Mbps"), 
                              "ControlMode", StringValue("OfdmRate6Mbps"),
                              "RtsCtsThreshold", UintegerValue(2200));

  wifiMacSat.SetType("ns3::AdhocWifiMac");
 // Time currentDelay = wifiMacSat.GetDefaultMaxPropagationDelay();
 // NS_LOG_UNCOND("Current max propagation delay: " << 
 // currentDelay.GetMicroSeconds() << " microseconds");


  // --- UPLINK CHANNEL (HAP -> Satellite) ---
  YansWifiPhyHelper wifiPhySatUp;
  wifiPhySatUp.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
  
  // НАСТРОЙКА ЗАДЕРЖКИ:
  // Slot = 150ms.
  // DIFS = 2 * Slot = 300ms.
  // Этого достаточно, чтобы "растянуть" внутренние таймеры MAC и дождаться ACK с RTT 240ms.
  // Скорость будет низкой из-за больших пауз, но потерь не будет.
  wifiPhySatUp.Set("Sifs", TimeValue(MicroSeconds(16))); 
  //wifiPhySatUp.Set("Slot", TimeValue(MilliSeconds(150))); 
  wifiPhySatUp.Set("Slot", TimeValue(MilliSeconds(300))); 

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
  
  wifiPhySatDown.Set("Sifs", TimeValue(MicroSeconds(16)));
  //wifiPhySatDown.Set("Slot", TimeValue(MilliSeconds(150)));
  wifiPhySatDown.Set("Slot", TimeValue(MilliSeconds(300)));

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

  // --- 4.5. Setup Link Traces ---
  SetupDeviceTraces(wifiDevicesA, "WiFi-A (Ground 1)");
  SetupDeviceTraces(wifiDevicesB, "WiFi-B (Ground 2)");
  SetupDeviceTraces(allSatDevices, "Ka-Sat");

// после установки всех устройств и перед SetupDeviceTraces:
NS_LOG_UNCOND("\n=== Checking Satellite Device Configuration ===");

if (satDevicesUp.GetN() > 0) {
    Ptr<WifiNetDevice> satDev = DynamicCast<WifiNetDevice>(satDevicesUp.Get(0));
    if (satDev) {
        Ptr<WifiPhy> phy = satDev->GetPhy();
        Ptr<WifiMac> mac = satDev->GetMac();
        
        NS_LOG_UNCOND("Satellite Uplink Info:");
        NS_LOG_UNCOND("  - MAC Type: " << mac->GetInstanceTypeId().GetName());
        
        // Получаем параметры через атрибуты (если нужно)
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
        
        // Получаем параметры через атрибуты (если нужно)
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
        
        // Получаем параметры через атрибуты (если нужно)
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
        
        // Получаем параметры через атрибуты (если нужно)
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
        NS_LOG_UNCOND("Group 1 WiFi Device Info:");
        NS_LOG_UNCOND("  - MAC Type: " << wifiDev->GetMac()->GetInstanceTypeId().GetName());
        NS_LOG_UNCOND("  - PHY Standard: 802.11b");
        NS_LOG_UNCOND("  - Data Mode: " << phyModeA);
        
        // Получаем параметры через атрибуты (если нужно)
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
  // Объединяем передатчики (HAP) и приемник (Спутник) в одну группу
  NetDeviceContainer uplinkNetworkDevices;
  uplinkNetworkDevices.Add(hapDevicesUp); // HAP 1 & 2
  uplinkNetworkDevices.Add(satDevicesUp); // Satellite
  
  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer interfacesSatUp = address.Assign (uplinkNetworkDevices); 
  
  // --- DOWNLINK NETWORK (10.1.4.0) ---
  // Объединяем передатчик (Спутник) и приемники (HAP) в одну группу
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
  // не может построить путь Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    // --- 9. Routing (Static Routing) ---
  // Используем статическую маршрутизацию вместо GlobalRouting, чтобы избежать 
  // конфликта "равноценных путей" (ECMP), который вызывает сбой assertion.
  
  Ipv4StaticRoutingHelper staticRoutingHelper;

  // --- Маршрутизация для наземных терминалов (UTs) ---
  
  // UT_1_1 и UT_1_2 отправляют всё на HAP_1 (шлюз по умолчанию)
  Ptr<Ipv4> ipv4Ut1_1 = nodes.Get(UT_1_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt1_1 = staticRoutingHelper.GetStaticRouting(ipv4Ut1_1);
  srUt1_1->SetDefaultRoute(interfacesWifiA.GetAddress(0), ipv4Ut1_1->GetInterfaceForAddress(interfacesWifiA.GetAddress(1)));

  Ptr<Ipv4> ipv4Ut1_2 = nodes.Get(UT_1_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt1_2 = staticRoutingHelper.GetStaticRouting(ipv4Ut1_2);
  srUt1_2->SetDefaultRoute(interfacesWifiA.GetAddress(0), ipv4Ut1_2->GetInterfaceForAddress(interfacesWifiA.GetAddress(2)));

  // UT_2_1 и UT_2_2 отправляют всё на HAP_2
  Ptr<Ipv4> ipv4Ut2_1 = nodes.Get(UT_2_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt2_1 = staticRoutingHelper.GetStaticRouting(ipv4Ut2_1);
  srUt2_1->SetDefaultRoute(interfacesWifiB.GetAddress(0), ipv4Ut2_1->GetInterfaceForAddress(interfacesWifiB.GetAddress(1)));

  Ptr<Ipv4> ipv4Ut2_2 = nodes.Get(UT_2_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srUt2_2 = staticRoutingHelper.GetStaticRouting(ipv4Ut2_2);
  srUt2_2->SetDefaultRoute(interfacesWifiB.GetAddress(0), ipv4Ut2_2->GetInterfaceForAddress(interfacesWifiB.GetAddress(2)));


  // --- Маршрутизация для HAP 1 ---
  // HAP 1 знает, что сеть 10.1.2.0 (Group 2) доступна через Спутник (через Uplink интерфейс)
  // Порядок IP в uplinkNetworkDevices: HAP_1 (0), HAP_2 (1), Sat (2)
  Ptr<Ipv4> ipv4Hap1 = nodes.Get(HAP_1)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srHap1 = staticRoutingHelper.GetStaticRouting(ipv4Hap1);
  // Dest: 10.1.2.0 (Ground 2), NextHop: Sat Uplink IP (interfacesSatUp.GetAddress(2)), Interface: HAP_1 Uplink
  srHap1->AddNetworkRouteTo(Ipv4Address("10.1.2.0"), Ipv4Mask("255.255.255.0"), 
                            interfacesSatUp.GetAddress(2), 
                            ipv4Hap1->GetInterfaceForAddress(interfacesSatUp.GetAddress(0)));


  // --- Маршрутизация для HAP 2 ---
  // HAP 2 знает, что сеть 10.1.1.0 (Group 1) доступна через Спутник (через Downlink интерфейс)
  // Порядок IP в downlinkNetworkDevices: Sat (0), HAP_1 (1), HAP_2 (2)
  Ptr<Ipv4> ipv4Hap2 = nodes.Get(HAP_2)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srHap2 = staticRoutingHelper.GetStaticRouting(ipv4Hap2);
  // Dest: 10.1.1.0 (Ground 1), NextHop: Sat Downlink IP (interfacesSatDown.GetAddress(0)), Interface: HAP_2 Downlink
  srHap2->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 
                            interfacesSatDown.GetAddress(0), 
                            ipv4Hap2->GetInterfaceForAddress(interfacesSatDown.GetAddress(2)));


  // --- Маршрутизация для Спутника ---
  // Спутник перенаправляет трафик между HAP 1 и HAP 2
  Ptr<Ipv4> ipv4Sat = nodes.Get(SATELLITE)->GetObject<Ipv4>();
  Ptr<Ipv4StaticRouting> srSat = staticRoutingHelper.GetStaticRouting(ipv4Sat);

  // Маршрут к Ground 1 (через Downlink к HAP 1)
  // NextHop: HAP_1 Downlink IP (interfacesSatDown.GetAddress(1))
  srSat->AddNetworkRouteTo(Ipv4Address("10.1.1.0"), Ipv4Mask("255.255.255.0"), 
                           interfacesSatDown.GetAddress(1), 
                           ipv4Sat->GetInterfaceForAddress(interfacesSatDown.GetAddress(0)));

  // Маршрут к Ground 2 (через Uplink к HAP 2)
  // NextHop: HAP_2 Uplink IP (interfacesSatUp.GetAddress(1))
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

  //double simTime = 1.0 + (numPackets * interPacketInterval.GetSeconds()) + 5.0;
  double simTime = 20.0;  
  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  // --- Link Level Statistics Output ---
  std::cout << "\n\n=== Per-Device Link Loss Statistics ===" << std::endl;
  
  // Выводим заголовок таблицы с выравниванием
  // std::left - выравнивание по левому краю (для имени)
  // std::right - выравнивание по правому краю (для цифр)
  // std::setw(N) - задает ширину поля в N символов
  std::cout << std::left << std::setw(45) << "Device Name" 
            << std::right << std::setw(10) << "Tx Pkts" 
            << std::right << std::setw(12) << "Rx Succ" 
            << std::right << std::setw(12) << "Rx Drop" 
            << std::right << std::setw(12) << "Loss %" << std::endl;
            
  std::cout << std::string(91, '-') << std::endl; // Линия-разделитель
  
  for (auto const& [device, name] : g_deviceNames) {
      LinkStats stats = g_statsMap[device];
      double lossRatio = 0.0;
      double inboundTotal = stats.rxPacketsSuccess + stats.rxDropped;
      
      if (inboundTotal > 0) {
          lossRatio = (stats.rxDropped / inboundTotal) * 100.0;
      }

      // Выводим данные с тем же выравниванием, что и заголовок
      // std::fixed и std::setprecision(2) ограничивают дробную часть до 2 знаков
      std::cout << std::left << std::setw(45) << name 
                << std::right << std::setw(10) << stats.txPackets
                << std::right << std::setw(12) << stats.rxPacketsSuccess 
                << std::right << std::setw(12) << stats.rxDropped 
                << std::right << std::setw(11) << std::fixed << std::setprecision(2) << lossRatio << "%" 
                << std::endl;
  }
  std::cout << std::string(91, '-') << std::endl;


  // --- ТАБЛИЦА: Детализация причин потерь ---
  std::cout << "\n=== Packet Drop Reasons Breakdown ===" << std::endl;
  
  std::cout << std::left << std::setw(45) << "Device Name" 
            << std::setw(30) << "Drop Reason" 
            << std::right << std::setw(10) << "Count" << std::endl;
            
  std::cout << std::string(85, '-') << std::endl;
  
  // Проходим по всем устройствам
  for (auto const& [device, name] : g_deviceNames) {
      LinkStats stats = g_statsMap[device];
      
      // Если у этого устройства есть потери
      if (!stats.dropReasons.empty()) {
          // Проходим по всем причинам для этого устройства
          for (auto const& [reason, count] : stats.dropReasons) {
              std::cout << std::left << std::setw(45) << name 
                        << std::setw(30) << reason 
                        << std::right << std::setw(10) << count << std::endl;
          }
      }
  }
  std::cout << std::string(85, '-') << std::endl;

  // --- Statistics ---
  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  std::cout << "\n=== Ka-band Satellite Simulation Results (End-to-End) ===" << std::endl;
  std::cout << "Topology: Ground WiFi <-> HAP (20km) <-> GEO Sat <-> HAP (20km) <-> Ground WiFi" << std::endl;

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
            
  std::cout << std::string(109, '-') << std::endl; // Новая длина линии

  // Вывод данных по каждому потоку
  for (auto const& [flowId, flowStats] : stats) {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flowId);
      
      // Получаем имена узлов по IP
      std::string srcNodeName = GetNodeNameByIp(t.sourceAddress, nodes);
      std::string dstNodeName = GetNodeNameByIp(t.destinationAddress, nodes);
      
      // Формируем строки адресов
      std::stringstream srcSs, dstSs;
      srcSs << t.sourceAddress << " [" << srcNodeName << "]";
      dstSs << t.destinationAddress << " [" << dstNodeName << "]";

      // Расчет потерь
      double lossRatio = 0.0;
      if (flowStats.txPackets > 0) {
          lossRatio = ((double)(flowStats.txPackets - flowStats.rxPackets) / flowStats.txPackets) * 100.0;
      }

      // Подготовка переменных
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
          
          // Вывод строки с метриками
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
          // Вывод строки, если Rx = 0
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