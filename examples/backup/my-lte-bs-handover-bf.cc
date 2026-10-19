#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/antenna-model.h" // <--- Необходимо для работы с антеннами
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
    // 1. Настраиваем параметры MIMO и бимформинга ДО создания LteHelper
    // Включаем режим передачи 2 (Transmit Diversity / Spatial Multiplexing 2x2)
    Config::SetDefault ("ns3::LteEnbRrc::DefaultTransmissionMode", UintegerValue (2)); 

    // Параметры алгоритма хэндовера
    Config::SetDefault ("ns3::A3RsrpHandoverAlgorithm::Hysteresis", DoubleValue (3.));
    Config::SetDefault ("ns3::A3RsrpHandoverAlgorithm::TimeToTrigger", TimeValue (MilliSeconds (256)));
    
    CommandLine cmd (__FILE__);
    cmd.Parse (argc, argv);
    LogComponentEnable ("LteUeRrc", LOG_LEVEL_INFO);
    LogComponentEnable ("A3RsrpHandoverAlgorithm", LOG_LEVEL_ALL);

    // 2. EPC & LTE Helper
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
    lteHelper->SetEpcHelper (epcHelper);
    
    // ---> НАСТРОЙКА АНТЕНН (БИМФОРМИНГ) <---
    uint16_t  antenna_selector = 1;

    if (antenna_selector == 0)
    {
        // Используем модель косинусной антенны (направленная)
        lteHelper->SetEnbAntennaModelType ("ns3::CosineAntennaModel");
        lteHelper->SetEnbAntennaModelAttribute ("MaxGain", DoubleValue (0.0));  // Максимальное усиление (дБи)
    }
    else
    {

        // Используем модель параболической антенны, она поддерживает Beamwidth
        lteHelper->SetEnbAntennaModelType ("ns3::ParabolicAntennaModel");
        lteHelper->SetEnbAntennaModelAttribute ("Beamwidth", DoubleValue (60)); // Ширина луча 60 градусов
     //   lteHelper->SetEnbAntennaModelAttribute ("MaxGain", DoubleValue (0.0));  // Максимальное усиление (дБи) 
    }                                                                         // -------------------------------------

    lteHelper->Initialize ();

    // Включаем алгоритм хэндовера на основе RSRP (A3 event)
    lteHelper->SetHandoverAlgorithmType ("ns3::A3RsrpHandoverAlgorithm");

    // 2.1 Внешний хост
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
    Ipv4Address serverAddress = internetIface.GetAddress (1);

    // 3. Параметры радио
    lteHelper->SetEnbDeviceAttribute ("DlEarfcn", UintegerValue (100));
    lteHelper->SetEnbDeviceAttribute ("UlEarfcn", UintegerValue (18100));
    lteHelper->SetEnbDeviceAttribute ("DlBandwidth", UintegerValue (50)); 
    lteHelper->SetSchedulerType ("ns3::RrFfMacScheduler");

    // 4. Создаём 2 базовые станции
    NodeContainer enbNodes;
    enbNodes.Create (2);
    MobilityHelper mobility;
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
            "MinX", DoubleValue (0.0),
            "MinY", DoubleValue (0.0),
            "DeltaX", DoubleValue (200.0),
            "DeltaY", DoubleValue (0.0),
            "GridWidth", UintegerValue (2),
            "LayoutType", StringValue ("RowFirst"));
    mobility.Install (enbNodes);
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);

    // ---> НАСТРОЙКА ОРИЕНТАЦИИ ЛУЧЕЙ <---
    if (antenna_selector == 0)
    {     
        // eNB1 (Node 0) на (0,0) смотрит на Восток (0 градусов)
        // eNB2 (Node 1) на (200,0) смотрит на Запад (180 градусов)
        // Мы используем Config::Set для доступа к атрибутам уже созданных устройств
        Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::CosineAntennaModel/Orientation", DoubleValue (0));
        Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/RxAntenna/$ns3::CosineAntennaModel/Orientation", DoubleValue (0));

        Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::CosineAntennaModel/Orientation", DoubleValue (180));
        Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/RxAntenna/$ns3::CosineAntennaModel/Orientation", DoubleValue (180));
    }
    else
    {
        // Обратите внимание на изменение имени класса в пути на $ns3::ParabolicAntennaModel
        Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ParabolicAntennaModel/Orientation", DoubleValue (0));
        Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/RxAntenna/$ns3::ParabolicAntennaModel/Orientation", DoubleValue (0));

        Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ParabolicAntennaModel/Orientation", DoubleValue (180));
        Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/RxAntenna/$ns3::ParabolicAntennaModel/Orientation", DoubleValue (180));
    }
    // -------------------------------------

    lteHelper->AddX2Interface (enbNodes);

    // 5. Создаём 1 UE
    NodeContainer ueNodes;
    ueNodes.Create (1);

    // Мобильность UE
    Ptr<ConstantVelocityMobilityModel> ueMob = CreateObject<ConstantVelocityMobilityModel> ();
    ueMob->SetPosition (Vector (50.0, 0.0, 1.5));
    ueMob->SetVelocity (Vector (15.0, 0.0, 0.0));
    ueNodes.Get (0)->AggregateObject (ueMob);

    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (ueNodes);

    // 6. IP стек и Attach
    InternetStackHelper internetUe;
    internetUe.Install (ueNodes);
    lteHelper->Attach (ueDevs, enbDevs.Get (0));

    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ueIface = ipv4.Assign (ueDevs);

    // 7. Трафик
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

    // 8. Трассировка
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
            MakeCallback (&BsHandoverStartTrace));
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
            MakeCallback (&BsHandoverEndTrace));

    PacketSinkHelper sink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
    apps = sink.Install (remoteHost.Get (0));
    apps.Start (Seconds (0.0));
    apps.Stop (Seconds (25.0));

    // 9. Запуск
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Simulator::Stop (Seconds (28.0));
    Simulator::Run ();

    monitor->CheckForLostPackets ();
    monitor->SerializeToXmlFile ("lte-flowmon.xml", true, true);
    Simulator::Destroy ();

    return 0;
}
