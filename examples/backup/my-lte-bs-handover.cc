#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
using namespace ns3;


//  Callback-функции для трассировки хэндовера

void BsHandoverStartTrace (uint64_t imsi, uint16_t cellId, uint16_t oldCellId, uint16_t newCellId)
{
    std::cout << "[HO START] Time=" << Simulator::Now().GetSeconds() << "s | IMSI=" << imsi
              << " | CellId: " << oldCellId << " -> " << newCellId << "\n";
}

void BsHandoverEndTrace (uint64_t imsi, uint16_t oldCellId, uint16_t newCellId)
{
    std::cout << "[HO END  ] Time=" << Simulator::Now().GetSeconds() << "s | IMSI=" << imsi
              << " | Now attached to CellId=" << newCellId << "\n";
}

int main (int argc, char *argv[])
{
    // Устанавливаем параметры ДО создания LteHelper и алгоритмов
    Config::SetDefault ("ns3::A3RsrpHandoverAlgorithm::Hysteresis", DoubleValue (3.));
    Config::SetDefault ("ns3::A3RsrpHandoverAlgorithm::TimeToTrigger", TimeValue (MilliSeconds (256)));
    CommandLine cmd (__FILE__);
    cmd.Parse (argc, argv);
    LogComponentEnable ("LteUeRrc", LOG_LEVEL_INFO);
    LogComponentEnable ("A3RsrpHandoverAlgorithm", LOG_LEVEL_ALL);

    // 1. EPC & LTE Helper
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
    lteHelper->SetEpcHelper (epcHelper);
    lteHelper->Initialize ();

    // Включаем алгоритм хэндовера на основе RSRP (A3 event)
    lteHelper->SetHandoverAlgorithmType ("ns3::A3RsrpHandoverAlgorithm");

    // 1.1 Внешний хост (реалистичная PDN за P-GW)
    Ptr<Node> pgw = epcHelper->GetPgwNode();
    NodeContainer remoteHost;
    remoteHost.Create (1);
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Mbps")));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (10)));
    NetDeviceContainer pgwDev = p2p.Install (pgw, remoteHost.Get (0));
    
    InternetStackHelper internet;
    internet.Install (remoteHost);
    Ipv4AddressHelper ipv4Internet;
    ipv4Internet.SetBase ("1.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer internetIface = ipv4Internet.Assign (pgwDev);
    Ipv4Address serverAddress = internetIface.GetAddress (1); // 1.0.0.2

    // 2. Параметры радио
    lteHelper->SetEnbDeviceAttribute ("DlEarfcn", UintegerValue (100));
    lteHelper->SetEnbDeviceAttribute ("UlEarfcn", UintegerValue (18100));
    lteHelper->SetEnbDeviceAttribute ("DlBandwidth", UintegerValue (50)); // ~10 МГц
    lteHelper->SetSchedulerType ("ns3::RrFfMacScheduler");

    // 3. Создаём 2 базовые станции (разнесены на 200 м)
    NodeContainer enbNodes;
    enbNodes.Create (2);
    MobilityHelper mobility;
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                   "MinX", DoubleValue (0.0),
                                   "MinY", DoubleValue (0.0),
                                   "DeltaX", DoubleValue (200.0), //200
                                   "DeltaY", DoubleValue (0.0),
                                   "GridWidth", UintegerValue (2),
                                   "LayoutType", StringValue ("RowFirst"));
    mobility.Install (enbNodes);
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);
    lteHelper->AddX2Interface (enbNodes); // Устанавливаем логическое соединение (интерфейс X2) между базовыми станциями.

    // 4. Создаём 1 UE для наглядной демонстрации хэндовера
    NodeContainer ueNodes;
    ueNodes.Create (1);
    
    // Мобильность: старт у eNB1, движение к eNB2 со скоростью 15 м/с
    Ptr<ConstantVelocityMobilityModel> ueMob = CreateObject<ConstantVelocityMobilityModel> ();
    ueMob->SetPosition (Vector (50.0, 0.0, 1.5));  // x=50м (ближе к eNB1 в x=0)
    ueMob->SetVelocity (Vector (15.0, 0.0, 0.0));  // v=15 м/с вдоль оси X
    ueNodes.Get (0)->AggregateObject (ueMob);

    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (ueNodes);

    // 5. Устанавливаем IP-стек ДО Attach (критично для ns-3)
    InternetStackHelper internetUe;
    internetUe.Install (ueNodes);

    // 6. Первоначальное подключение к первой БС
    lteHelper->Attach (ueDevs, enbDevs.Get (0));

    // Назначаем IP в подсети радиоинтерфейса
    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ueIface = ipv4.Assign (ueDevs);

    // 7. Генератор трафика (UE -> внешний сервер)
    uint16_t port = 5000;
    Address remoteAddress (InetSocketAddress (serverAddress, port));
    OnOffHelper onoff ("ns3::UdpSocketFactory", remoteAddress);
    onoff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));
    onoff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));
    onoff.SetAttribute ("DataRate", DataRateValue (DataRate ("2Mbps")));
    onoff.SetAttribute ("PacketSize", UintegerValue (1024));
    
    ApplicationContainer apps = onoff.Install (ueNodes.Get (0));
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (25.0));
     
    // 8. Трассировка хэндовера
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                                   MakeCallback (&BsHandoverStartTrace));
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                                   MakeCallback (&BsHandoverEndTrace));


    // Сервер на внешнем хосте
    PacketSinkHelper sink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
    apps = sink.Install (remoteHost.Get (0));
    apps.Start (Seconds (0.0));
    apps.Stop (Seconds (25.0));

    // 9. Запуск симуляции и FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();
    
    Simulator::Stop (Seconds (28.0));
    Simulator::Run ();
    
    monitor->CheckForLostPackets ();
    monitor->SerializeToXmlFile ("lte-flowmon.xml", true, true);
    Simulator::Destroy ();
    
    return 0;
}
