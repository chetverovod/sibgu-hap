#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#define TEST_NAME "hapsimulator"

using namespace ns3;

int main (int argc, char *argv[])
{
  NS_LOG_COMPONENT_DEFINE(TEST_NAME);
  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  // 1. EPC (ядро сети)
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  lteHelper->SetEpcHelper (epcHelper);
  lteHelper->Initialize ();

  // 1.1 Получаем P-GW и создаём внешний хост
Ptr<Node> pgw = epcHelper->GetPgwNode();
NodeContainer remoteHost;
remoteHost.Create (1);

PointToPointHelper p2p;
p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Mbps")));
p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (10)));
NetDeviceContainer pgwDev = p2p.Install (pgw, remoteHost.Get (0));

// Стек и адресация для внешней сети
InternetStackHelper internet;
internet.Install (remoteHost);
Ipv4AddressHelper ipv4Internet;
ipv4Internet.SetBase ("1.0.0.0", "255.255.255.0");
Ipv4InterfaceContainer internetIface = ipv4Internet.Assign (pgwDev);
Ipv4Address serverAddress = internetIface.GetAddress (1);  // 1.0.0.2

  // 2. Параметры радио
  lteHelper->SetEnbDeviceAttribute ("DlEarfcn", UintegerValue (100)); // Частота DL
  lteHelper->SetEnbDeviceAttribute ("UlEarfcn", UintegerValue (18100)); // Частота UL
  lteHelper->SetEnbDeviceAttribute ("DlBandwidth", UintegerValue (50)); // 50 RB ≈ 10 МГц
  
  lteHelper->SetSchedulerType ("ns3::RrFfMacScheduler"); // Round Robin

  // 3. Создание БС
  NodeContainer enbNodes;
  enbNodes.Create (1);
  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (0.0),
                                 "MinY", DoubleValue (0.0),
                                 "DeltaX", DoubleValue (100.0),
                                 "DeltaY", DoubleValue (100.0),
                                 "GridWidth", UintegerValue (1),
                                 "LayoutType", StringValue ("RowFirst"));
  mobility.Install (enbNodes);

  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);

  // 4. Создание UE
  NodeContainer ueNodes;
  ueNodes.Create (2);
  mobility.SetPositionAllocator ("ns3::RandomBoxPositionAllocator",
                                 "X", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=200.0]"),
                                 "Y", StringValue ("ns3::UniformRandomVariable[Min=0.0|Max=200.0]"),
                                 "Z", StringValue ("ns3::ConstantRandomVariable[Constant=1.5]"));
  mobility.Install (ueNodes);

  NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (ueNodes);

  

  // 5. IP-адресация (автоматически через EPC)
 // InternetStackHelper internet;
  internet.Install (ueNodes);

  // 6. Attach UE к БС
  lteHelper->Attach (ueDevs, enbDevs.Get (0));

  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer ueIface = ipv4.Assign (ueDevs);


  // Назначение адресов на радиоинтерфейсе (другая подсеть!)
//Ipv4AddressHelper ipv4Radio;
//ipv4Radio.SetBase ("10.0.0.0", "255.255.255.0");
//ipv4Radio.Assign (ueDevs);


// Трафик: теперь цель — реальный сервер в интернете
uint16_t port = 5000;
Address remoteAddress (InetSocketAddress (serverAddress, port));  // ← 1.0.0.2
OnOffHelper onoff ("ns3::UdpSocketFactory", remoteAddress);

  // 7. Трафик (UDP от UE к серверу в интернете)
 // uint16_t port = 5000;
 //  Address remoteAddress (InetSocketAddress (Ipv4Address ("1.0.0.1"), port));
 //  OnOffHelper onoff ("ns3::UdpSocketFactory", remoteAddress);
  onoff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));
  onoff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));
  onoff.SetAttribute ("DataRate", DataRateValue (DataRate ("1Mbps")));
  onoff.SetAttribute ("PacketSize", UintegerValue (1024));
  ApplicationContainer apps = onoff.Install (ueNodes.Get (0));
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));


  // Сервер на внешнем узле
  PacketSinkHelper sink ("ns3::UdpSocketFactory",
                       InetSocketAddress (Ipv4Address::GetAny (), port));
  apps = sink.Install (remoteHost.Get (0));  //  реалистичное расположение
  // Сервер
  //PacketSinkHelper sink ("ns3::UdpSocketFactory",
  //        InetSocketAddress (Ipv4Address::GetAny (), port));
 // apps = sink.Install (enbNodes.Get (0)); // Упрощённо: сервер на БС
  apps.Start (Seconds (0.0));
  apps.Stop (Seconds (10.0));

  // 8. Трассировка и мониторинг
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (12.0));
  Simulator::Run ();

  monitor->CheckForLostPackets ();
  monitor->SerializeToXmlFile ("lte-flowmon.xml", true, true);


  Simulator::Destroy ();
  return 0;
}
