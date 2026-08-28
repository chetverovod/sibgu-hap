/*
Данный тест симулирует  базовую станцию расположенную на борту ВСП.
*/

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/lte-module.h"
#include "ns3/lte-enb-phy.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/antenna-model.h"
#include "ns3/three-gpp-antenna-model.h"
#include "ns3/double.h"
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("my-lte-bs-handover-bf-hap");

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
    // 1. Параметры MIMO и алгоритма хэндовера
    Config::SetDefault ("ns3::LteEnbRrc::DefaultTransmissionMode", UintegerValue (2)); 
    Config::SetDefault ("ns3::A3RsrpHandoverAlgorithm::Hysteresis", DoubleValue (3.));
    Config::SetDefault ("ns3::A3RsrpHandoverAlgorithm::TimeToTrigger", TimeValue (MilliSeconds (256)));
    
    CommandLine cmd (__FILE__);
    cmd.Parse (argc, argv);
    LogComponentEnable ("LteUeRrc", LOG_LEVEL_INFO);

    // 2. EPC & LTE Helper
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
    lteHelper->SetEpcHelper (epcHelper);
    
    // ---> НАСТРОЙКА АНТЕНН <---
    // Используем 3GPP модель для поддержки 3D наклона (Tilt)
    lteHelper->SetEnbAntennaModelType ("ns3::ThreeGppAntennaModel");
    
    // ВНИМАНИЕ: ThreeGppAntennaModel НЕ имеет атрибута MaxGain.
    // Усинение рассчитывается внутри модели автоматически.
    
    lteHelper->Initialize ();

    // Включаем алгоритм хэндовера
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
    
    // Стандартная мощность для наземной БС
    Config::SetDefault ("ns3::LteEnbPhy::TxPower", DoubleValue (43.0)); 

    // 4. Создаём 2 базовые станции
    NodeContainer enbNodes;
    enbNodes.Create (2); // Node 0 будет HAP, Node 1 наземной

    // --- eNB 0: Летает на HAP ---
    Ptr<ConstantVelocityMobilityModel> enbMobHap = CreateObject<ConstantVelocityMobilityModel> ();
    enbMobHap->SetPosition (Vector (0.0, 0.0, 30000.0)); // Высота 30 км
    enbMobHap->SetVelocity (Vector (300.0, 0.0, 0.0));  // Скорость 100 м/с
    enbNodes.Get (0)->AggregateObject (enbMobHap);

    // --- eNB 1: Статичная наземная ---
    // Помещаем ее далеко (10 км), чтобы принять хэндовер, когда HAP улетит
    Ptr<ConstantPositionMobilityModel> enbMobGnd = CreateObject<ConstantPositionMobilityModel> ();
    enbMobGnd->SetPosition (Vector (10000.0, 0.0, 30.0)); // X=10000, высота 30м
    enbNodes.Get (1)->AggregateObject (enbMobGnd);

    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);


    // ---> НАСТРОЙКА ОРИЕНТАЦИИ ЛУЧЕЙ <---
    
    // eNB 0 (HAP): Направляем антенну ВНИЗ (Downtilt +90), чтобы "светить" на землю
    // Bearing 0 (Восток) - вдоль полета
    Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/HorizontalBeamwidth", DoubleValue (60));
    Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/VerticalBeamwidth", DoubleValue (10));// было 60
    Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/Bearing", DoubleValue (0));
    Config::Set ("/NodeList/0/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/Downtilt", DoubleValue (90)); // +90 означает строго вниз

    // eNB 1 (Наземная): Направляем на Запад (180), чтобы "видеть" приближающийся UE
    Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/HorizontalBeamwidth", DoubleValue (60));
    Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/VerticalBeamwidth", DoubleValue (60));
    Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/Bearing", DoubleValue (180));
    Config::Set ("/NodeList/1/DeviceList/0/$ns3::LteEnbNetDevice/LteEnbPhy/TxAntenna/$ns3::ThreeGppAntennaModel/Downtilt", DoubleValue (0)); // Горизонтально

    // ---> Увеличиваем мощность HAP (eNB 0), чтобы он мог конкурировать с наземной БС <---
     // Получаем указатель на eNB устройство
    Ptr<NetDevice> nd = enbDevs.Get(0);

    // Приводим к типу LteEnbNetDevice
    Ptr<LteEnbNetDevice> lteEnbDev = DynamicCast<LteEnbNetDevice> (nd);
    NS_LOG_UNCOND("TxPower PHY check.");
    Ptr<LteEnbPhy> enbPhy = lteEnbDev->GetPhy();
    if (enbPhy) {
       // enbPhy->SetTxPower(49.0);
        enbPhy->SetAttribute("TxPower", DoubleValue(46.0));
        ns3::DoubleValue dv;
        // GetAttributeFailSafe возвращает bool: true если атрибут найден и тип совпал
        bool success = enbPhy->GetAttributeFailSafe("TxPower", dv);
        if (success) {
            double txPower = dv.Get();
            NS_LOG_UNCOND("TxPower из PHY = " << txPower << " dBm");
        } else {
            NS_LOG_UNCOND("Атрибут 'TxPower' не найден в LteEnbPhy");
        }
    } else {
        NS_LOG_UNCOND("Не удалось получить указатель на LteEnbPhy");
    }


    // Устанавливаем между базовыми станциями логический канал обмена
    lteHelper->AddX2Interface (enbNodes);

    // 5. Создаём UE (Наземный телефон)
    NodeContainer ueNodes;
    ueNodes.Create (1); 

    // --- UE 0: Телефон на земле ---
    Ptr<ConstantVelocityMobilityModel> ueMob = CreateObject<ConstantVelocityMobilityModel> ();
    ueMob->SetPosition (Vector (0.0, 0.0, 1.5)); // Стартует под HAP
    ueMob->SetVelocity (Vector (5.0, 0.0, 0.0));  // Движется медленно (5 м/с)
    ueNodes.Get (0)->AggregateObject (ueMob);

    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (ueNodes);

    // 6. IP стек и Attach
    InternetStackHelper internetUe;
    internetUe.Install (ueNodes);

    // Прикрепляем UE к eNB 0 (HAP)
    lteHelper->Attach (ueDevs.Get (0), enbDevs.Get (0));

    Ipv4AddressHelper ipv4;
    ipv4.SetBase ("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer ueIface = ipv4.Assign (ueDevs);

    // 7. Генератор трафика
    uint16_t port = 5000;
    Address remoteAddress (InetSocketAddress (serverAddress, port));

    OnOffHelper onoff ("ns3::UdpSocketFactory", remoteAddress);
    onoff.SetAttribute ("OnTime", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));
    onoff.SetAttribute ("OffTime", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"));
    onoff.SetAttribute ("DataRate", DataRateValue (DataRate ("2Mbps")));
    onoff.SetAttribute ("PacketSize", UintegerValue (1024));

    ApplicationContainer apps = onoff.Install (ueNodes.Get (0));
    apps.Start (Seconds (1.0));
    apps.Stop (Seconds (60.0)); 

    // 8. Трассировка хэндовера
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
            MakeCallback (&BsHandoverStartTrace));
    Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
            MakeCallback (&BsHandoverEndTrace));

    // Сервер
    PacketSinkHelper sink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), port));
    ApplicationContainer sinkApps = sink.Install (remoteHost.Get (0));
    sinkApps.Start (Seconds (0.0));
    sinkApps.Stop (Seconds (60.0));

    // 9. Запуск
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

//    Simulator::Stop (Seconds (65.0));
    Simulator::Stop (Seconds (300.0));
    Simulator::Run ();

    monitor->CheckForLostPackets ();
    monitor->SerializeToXmlFile ("lte-flowmon.xml", true, true);
    Simulator::Destroy ();

    return 0;
}
