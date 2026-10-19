#include "lte-cellular-helper.h"

#include "ns3/applications-module.h"
#include "ns3/config.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/geo-coordinate.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/lte-enb-net-device.h"
#include "ns3/lte-enb-phy.h"
#include "ns3/lte-helper.h"
#include "ns3/node.h"
#include "ns3/point-to-point-epc-helper.h"
#include "ns3/string.h"
#include "ns3/point-to-point-module.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/waypoint-mobility-model.h"

#include <fstream>
#include <filesystem>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("LteCellularHelper");

namespace ns3
{

namespace
{

std::ifstream
OpenText(const std::string& path)
{
    std::ifstream in(path);
    return in;
}

// Matches examples/lte_*.json interface_templates.lte_access.antenna.gain_dbi.
// enb.conf has no gain column; 30 dBm + 0 dBi isotropic at ~31 km is below LTE Qout.
constexpr double kLteIsotropicGainDbi = 5.0;

} // namespace

LteCellularHelper::LteCellularHelper() = default;

LteCellularHelper::~LteCellularHelper() = default;

Ptr<LteHelper>
LteCellularHelper::GetLteHelper() const
{
    return m_lteHelper;
}

void
LteCellularHelper::ConfigureLenaTraceOutput(const std::string& outputDir)
{
    if (outputDir.empty())
    {
        return;
    }
    auto put = [&](const char* attr, const char* name) {
        Config::SetDefault(attr, StringValue(Join(outputDir, name)));
    };
    put("ns3::PhyStatsCalculator::DlRsrpSinrFilename", "DlRsrpSinrStats.txt");
    put("ns3::PhyStatsCalculator::UlSinrFilename", "UlSinrStats.txt");
    put("ns3::PhyStatsCalculator::UlInterferenceFilename", "UlInterferenceStats.txt");
    put("ns3::MacStatsCalculator::DlOutputFilename", "DlMacStats.txt");
    put("ns3::MacStatsCalculator::UlOutputFilename", "UlMacStats.txt");
    put("ns3::PhyTxStatsCalculator::DlTxOutputFilename", "DlTxPhyStats.txt");
    put("ns3::PhyTxStatsCalculator::UlTxOutputFilename", "UlTxPhyStats.txt");
    put("ns3::PhyRxStatsCalculator::DlRxOutputFilename", "DlRxPhyStats.txt");
    put("ns3::PhyRxStatsCalculator::UlRxOutputFilename", "UlRxPhyStats.txt");
    put("ns3::RadioBearerStatsCalculator::DlRlcOutputFilename", "DlRlcStats.txt");
    put("ns3::RadioBearerStatsCalculator::UlRlcOutputFilename", "UlRlcStats.txt");
    put("ns3::RadioBearerStatsCalculator::DlPdcpOutputFilename", "DlPdcpStats.txt");
    put("ns3::RadioBearerStatsCalculator::UlPdcpOutputFilename", "UlPdcpStats.txt");
}

void
LteCellularHelper::EnableLenaTraces()
{
    if (!m_lteHelper)
    {
        return;
    }
    m_lteHelper->EnableTraces();
    NS_LOG_UNCOND("[lte] LENA traces enabled (DlRsrpSinrStats.txt and related files)");
}

bool
LteCellularHelper::SkipComment(const std::string& line)
{
    if (line.empty())
    {
        return true;
    }
    return line[0] == '%' || line[0] == '#';
}

std::string
LteCellularHelper::Basename(const std::string& path)
{
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
    {
        return path;
    }
    return path.substr(pos + 1);
}

std::string
LteCellularHelper::Join(const std::string& root, const std::string& rel)
{
    if (root.empty())
    {
        return rel;
    }
    if (!rel.empty() && (rel[0] == '/' || (rel.size() > 1 && rel[1] == ':')))
    {
        return rel;
    }
    if (root.back() == '/' || root.back() == '\\')
    {
        return root + rel;
    }
    return root + "/" + rel;
}

bool
LteCellularHelper::FileExists(const std::string& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool
LteCellularHelper::HasCellularScenario(const std::string& scenarioRoot)
{
    return FileExists(Join(scenarioRoot, "cellular/enb.conf")) ||
           FileExists(Join(scenarioRoot, "cellular/enb_positions.txt"));
}

void
LteCellularHelper::EnsureUtPositionsFile(const std::string& scenarioRoot)
{
    const std::string path = Join(scenarioRoot, "positions/ut_positions.txt");
    if (FileExists(path))
    {
        return;
    }
    fs::create_directories(Join(scenarioRoot, "positions"));
    std::ofstream out(path);
    // SatConf::LoadPositions requires the file to exist; an empty file yields zero UTs.
    out << "";
}

uint16_t
LteCellularHelper::MhzToResourceBlocks(double bandwidthMhz)
{
    if (bandwidthMhz >= 20.0)
    {
        return 100;
    }
    if (bandwidthMhz >= 15.0)
    {
        return 75;
    }
    if (bandwidthMhz >= 10.0)
    {
        return 50;
    }
    if (bandwidthMhz >= 5.0)
    {
        return 25;
    }
    if (bandwidthMhz >= 3.0)
    {
        return 15;
    }
    return 6;
}

uint32_t
LteCellularHelper::UlEarfcnFromDl(uint32_t dlEarfcn)
{
    // Band 1 (EARFCN 0-599): UL = DL + 18000.
    if (dlEarfcn < 600)
    {
        return dlEarfcn + 18000;
    }
    return dlEarfcn;
}

std::string
LteCellularHelper::ResolveTracePath(const std::string& scenarioRoot, const std::string& rel)
{
    if (rel.empty() || rel == "-")
    {
        return {};
    }
    const std::string direct = Join(scenarioRoot, rel);
    if (FileExists(direct))
    {
        return direct;
    }
    const std::string byName = Join(Join(scenarioRoot, "positions"), Basename(rel));
    if (FileExists(byName))
    {
        return byName;
    }
    const std::string cellular = Join(Join(scenarioRoot, "cellular"), Basename(rel));
    if (FileExists(cellular))
    {
        return cellular;
    }
    return {};
}

std::vector<LteEnbRecord>
LteCellularHelper::LoadEnbs(const std::string& scenarioRoot)
{
    std::vector<LteEnbRecord> enbs;
    const std::string confPath = Join(scenarioRoot, "cellular/enb.conf");
    const std::string posPath = Join(scenarioRoot, "cellular/enb_positions.txt");
    if (!FileExists(confPath) || !FileExists(posPath))
    {
        return enbs;
    }

    std::ifstream posIn = OpenText(posPath);
    std::string line;
    std::vector<std::tuple<double, double, double>> coords;
    while (std::getline(posIn, line))
    {
        if (SkipComment(line))
        {
            continue;
        }
        std::istringstream iss(line);
        double lat = 0.0;
        double lon = 0.0;
        double alt = 0.0;
        if (!(iss >> lat >> lon >> alt))
        {
            continue;
        }
        coords.emplace_back(lat, lon, alt);
    }

    std::ifstream confIn = OpenText(confPath);
    while (std::getline(confIn, line))
    {
        if (SkipComment(line))
        {
            continue;
        }
        std::istringstream iss(line);
        LteEnbRecord rec;
        if (!(iss >> rec.enbId >> rec.cellId >> rec.earfcn >> rec.bandwidthMhz >> rec.txPowerDbm >>
              rec.mount >> rec.carrierId))
        {
            continue;
        }
        if (rec.enbId < coords.size())
        {
            rec.lat = std::get<0>(coords[rec.enbId]);
            rec.lon = std::get<1>(coords[rec.enbId]);
            rec.alt = std::get<2>(coords[rec.enbId]);
        }
        enbs.push_back(rec);
    }

    const std::string tracesPath = Join(scenarioRoot, "cellular/enb_traces.txt");
    if (FileExists(tracesPath))
    {
        std::ifstream trIn = OpenText(tracesPath);
        while (std::getline(trIn, line))
        {
            if (SkipComment(line))
            {
                continue;
            }
            std::istringstream iss(line);
            uint32_t id = 0;
            std::string rel;
            if (!(iss >> id >> rel))
            {
                continue;
            }
            for (auto& rec : enbs)
            {
                if (rec.enbId == id)
                {
                    rec.traceRelPath = rel;
                    break;
                }
            }
        }
    }
    return enbs;
}

std::vector<LteUeRecord>
LteCellularHelper::LoadUes(const std::string& scenarioRoot)
{
    std::vector<LteUeRecord> ues;
    const std::string confPath = Join(scenarioRoot, "cellular/ue.conf");
    const std::string posPath = Join(scenarioRoot, "cellular/ue_positions.txt");
    if (!FileExists(posPath))
    {
        return ues;
    }

    std::ifstream posIn = OpenText(posPath);
    std::string line;
    while (std::getline(posIn, line))
    {
        if (SkipComment(line))
        {
            continue;
        }
        std::istringstream iss(line);
        LteUeRecord rec;
        rec.ueId = ues.size();
        if (!(iss >> rec.lat >> rec.lon >> rec.alt))
        {
            continue;
        }
        ues.push_back(rec);
    }

    if (FileExists(confPath))
    {
        std::ifstream confIn = OpenText(confPath);
        uint32_t row = 0;
        while (std::getline(confIn, line))
        {
            if (SkipComment(line))
            {
                continue;
            }
            std::istringstream iss(line);
            uint32_t ueId = 0;
            std::string served;
            std::string mount;
            std::string carrier;
            if (!(iss >> ueId >> served))
            {
                continue;
            }
            if (ueId < ues.size())
            {
                if (served != "-")
                {
                    ues[ueId].servedEnbId = std::stoi(served);
                }
                if (iss >> mount >> carrier)
                {
                    ues[ueId].mount = mount;
                    ues[ueId].carrierId = carrier;
                }
            }
            (void)row;
            ++row;
        }
    }

    const std::string tracesPath = Join(scenarioRoot, "cellular/ue_traces.txt");
    if (FileExists(tracesPath))
    {
        std::ifstream trIn = OpenText(tracesPath);
        while (std::getline(trIn, line))
        {
            if (SkipComment(line))
            {
                continue;
            }
            std::istringstream iss(line);
            uint32_t id = 0;
            std::string rel;
            if (!(iss >> id >> rel))
            {
                continue;
            }
            if (id < ues.size())
            {
                ues[id].traceRelPath = rel;
            }
        }
    }
    return ues;
}

std::optional<uint32_t>
LteCellularHelper::FindOrbiterSatId(const std::string& scenarioRoot,
                                    const std::string& mount,
                                    const std::string& carrierId,
                                    const std::string& traceRelPath)
{
    if (mount != "airborne")
    {
        return std::nullopt;
    }

    if (!traceRelPath.empty())
    {
        const std::string want = Basename(traceRelPath);
        const std::string satTraces = Join(scenarioRoot, "positions/sat_traces.txt");
        if (FileExists(satTraces))
        {
            std::ifstream in = OpenText(satTraces);
            std::string line;
            while (std::getline(in, line))
            {
                if (SkipComment(line))
                {
                    continue;
                }
                std::istringstream iss(line);
                uint32_t satId = 0;
                std::string path;
                if (!(iss >> satId >> path))
                {
                    continue;
                }
                if (Basename(path) == want)
                {
                    return satId;
                }
            }
        }
    }

    const std::string tles = Join(scenarioRoot, "positions/tles.txt");
    if (!FileExists(tles))
    {
        return std::nullopt;
    }
    std::ifstream in = OpenText(tles);
    std::string header;
    std::getline(in, header);
    std::vector<uint32_t> hapIds;
    std::string nameLine;
    while (std::getline(in, nameLine))
    {
        if (nameLine.empty())
        {
            continue;
        }
        std::istringstream iss(nameLine);
        uint32_t satId = 0;
        std::string name;
        if (!(iss >> satId >> name))
        {
            continue;
        }
        if (name.rfind("HAP_Trace_", 0) == 0 || name == carrierId)
        {
            hapIds.push_back(satId);
        }
        std::string dummy;
        std::getline(in, dummy);
        std::getline(in, dummy);
    }
    if (carrierId != "-" && hapIds.size() == 1)
    {
        return hapIds.front();
    }
    if (hapIds.size() == 1)
    {
        return hapIds.front();
    }
    return std::nullopt;
}

Ptr<Node>
LteCellularHelper::PlaceOnCarrier(const std::string& scenarioRoot,
                                  Ptr<SatTopology> topology,
                                  const std::string& mount,
                                  const std::string& carrierId,
                                  const std::string& traceRelPath,
                                  double lat,
                                  double lon,
                                  double alt)
{
    if (topology)
    {
        const auto satId =
            FindOrbiterSatId(scenarioRoot, mount, carrierId, traceRelPath);
        NodeContainer orbiters = topology->GetOrbiterNodes();
        if (satId.has_value() && *satId < orbiters.GetN())
        {
            return orbiters.Get(*satId);
        }
    }
    Ptr<Node> node = CreateObject<Node>();
    InstallGeoMobility(node, lat, lon, alt, ResolveTracePath(scenarioRoot, traceRelPath));
    return node;
}

void
LteCellularHelper::InstallGeoMobility(Ptr<Node> node,
                                      double lat,
                                      double lon,
                                      double alt,
                                      const std::string& tracePath)
{
    // LENA Friis uses MobilityModel::GetDistanceFrom() on cartesian metres.
    // GeoCoordinate::ToVector() is ECEF; do not use lat/lon as x/y.
    GeoCoordinate geo(lat, lon, alt);
    const Vector cart = geo.ToVector();

    if (tracePath.empty() || !FileExists(tracePath))
    {
        Ptr<ConstantPositionMobilityModel> mob = CreateObject<ConstantPositionMobilityModel>();
        mob->SetPosition(cart);
        node->AggregateObject(mob);
        return;
    }

    Ptr<WaypointMobilityModel> mob = CreateObject<WaypointMobilityModel>();
    std::ifstream in = OpenText(tracePath);
    std::string line;
    uint32_t added = 0;
    while (std::getline(in, line))
    {
        if (SkipComment(line))
        {
            continue;
        }
        std::istringstream iss(line);
        double t = 0.0;
        double wlat = 0.0;
        double wlon = 0.0;
        double walt = 0.0;
        if (!(iss >> t >> wlat >> wlon >> walt))
        {
            continue;
        }
        GeoCoordinate wpGeo(wlat, wlon, walt);
        mob->AddWaypoint(Waypoint(Seconds(t), wpGeo.ToVector()));
        ++added;
    }
    if (added == 0)
    {
        Ptr<ConstantPositionMobilityModel> fallback = CreateObject<ConstantPositionMobilityModel>();
        fallback->SetPosition(cart);
        node->AggregateObject(fallback);
        return;
    }
    node->AggregateObject(mob);
}

bool
LteCellularHelper::Install(const std::string& scenarioRoot,
                           Ptr<SatTopology> topology,
                           Time simulationStop,
                           const std::string& outputDir)
{
    const auto enbs = LoadEnbs(scenarioRoot);
    const auto ues = LoadUes(scenarioRoot);
    if (enbs.empty())
    {
        NS_LOG_UNCOND("[lte] No eNB records in scenario/cellular/; skipping LENA.");
        return false;
    }

    // Before CreateObject<LteHelper>: PHY/MAC calculators read filenames in DoInitialize.
    ConfigureLenaTraceOutput(outputDir);

    Config::SetDefault("ns3::LteEnbPhy::TxPower", DoubleValue(enbs.front().txPowerDbm));
    // Typical LTE Class-3 UE (23 dBm). Default 10 dBm plus 5 dBi cannot close the
    // ~31 km UL to a 30 km HAPS (eNB NF ~5 dB, 20 MHz).
    Config::SetDefault("ns3::LteUePhy::TxPower", DoubleValue(23.0));

    m_epcHelper = CreateObject<PointToPointEpcHelper>();
    m_lteHelper = CreateObject<LteHelper>();
    m_lteHelper->SetEpcHelper(m_epcHelper);
    m_lteHelper->SetSchedulerType("ns3::RrFfMacScheduler");
    // Apply isotropic gain before Install*Device; factory attributes are read then.
    m_lteHelper->SetEnbAntennaModelType("ns3::IsotropicAntennaModel");
    m_lteHelper->SetEnbAntennaModelAttribute("Gain", DoubleValue(kLteIsotropicGainDbi));
    m_lteHelper->SetUeAntennaModelType("ns3::IsotropicAntennaModel");
    m_lteHelper->SetUeAntennaModelAttribute("Gain", DoubleValue(kLteIsotropicGainDbi));
    m_lteHelper->SetEnbDeviceAttribute("DlEarfcn", UintegerValue(enbs.front().earfcn));
    m_lteHelper->SetEnbDeviceAttribute("UlEarfcn",
                                       UintegerValue(UlEarfcnFromDl(enbs.front().earfcn)));
    m_lteHelper->SetEnbDeviceAttribute("DlBandwidth",
                                       UintegerValue(MhzToResourceBlocks(enbs.front().bandwidthMhz)));
    m_lteHelper->SetEnbDeviceAttribute("UlBandwidth",
                                       UintegerValue(MhzToResourceBlocks(enbs.front().bandwidthMhz)));

    for (const auto& rec : enbs)
    {
        Ptr<Node> node = PlaceOnCarrier(scenarioRoot,
                                        topology,
                                        rec.mount,
                                        rec.carrierId,
                                        rec.traceRelPath,
                                        rec.lat,
                                        rec.lon,
                                        rec.alt);
        const auto satId =
            FindOrbiterSatId(scenarioRoot, rec.mount, rec.carrierId, rec.traceRelPath);
        if (satId.has_value() && topology && *satId < topology->GetOrbiterNodes().GetN() &&
            node == topology->GetOrbiterNodes().Get(*satId))
        {
            NS_LOG_UNCOND("[lte] eNB " << rec.enbId << " payload on orbiter satId=" << *satId
                                       << " carrier=" << rec.carrierId);
        }
        else
        {
            NS_LOG_UNCOND("[lte] eNB " << rec.enbId << " standalone node mount=" << rec.mount
                                       << " at " << rec.lat << "," << rec.lon << "," << rec.alt);
        }
        m_enbNodes.Add(node);
    }

    m_enbDevs = m_lteHelper->InstallEnbDevice(m_enbNodes);

    for (uint32_t i = 0; i < enbs.size() && i < m_enbDevs.GetN(); ++i)
    {
        Ptr<LteEnbNetDevice> enbDev = DynamicCast<LteEnbNetDevice>(m_enbDevs.Get(i));
        if (!enbDev)
        {
            continue;
        }
        Ptr<LteEnbPhy> phy = enbDev->GetPhy();
        if (phy)
        {
            phy->SetAttribute("TxPower", DoubleValue(enbs[i].txPowerDbm));
        }
        enbDev->SetDlEarfcn(enbs[i].earfcn);
        enbDev->SetUlEarfcn(UlEarfcnFromDl(enbs[i].earfcn));
        const uint16_t rb = MhzToResourceBlocks(enbs[i].bandwidthMhz);
        enbDev->SetDlBandwidth(rb);
        enbDev->SetUlBandwidth(rb);
    }

    if (m_enbNodes.GetN() > 1)
    {
        m_lteHelper->AddX2Interface(m_enbNodes);
    }

    for (const auto& rec : ues)
    {
        Ptr<Node> node = PlaceOnCarrier(scenarioRoot,
                                        topology,
                                        rec.mount,
                                        rec.carrierId,
                                        rec.traceRelPath,
                                        rec.lat,
                                        rec.lon,
                                        rec.alt);
        const auto satId =
            FindOrbiterSatId(scenarioRoot, rec.mount, rec.carrierId, rec.traceRelPath);
        if (satId.has_value() && topology && *satId < topology->GetOrbiterNodes().GetN() &&
            node == topology->GetOrbiterNodes().Get(*satId))
        {
            NS_LOG_UNCOND("[lte] UE " << rec.ueId << " payload on orbiter satId=" << *satId
                                      << " carrier=" << rec.carrierId);
        }
        else
        {
            NS_LOG_UNCOND("[lte] UE " << rec.ueId << " at " << rec.lat << "," << rec.lon << ","
                                      << rec.alt);
        }
        m_ueNodes.Add(node);
    }

    if (m_ueNodes.GetN() == 0)
    {
        NS_LOG_UNCOND("[lte] No UE records; eNB installed without subscribers.");
        EnableLenaTraces();
        return true;
    }

    m_ueDevs = m_lteHelper->InstallUeDevice(m_ueNodes);

    InternetStackHelper internet;
    for (uint32_t i = 0; i < m_ueNodes.GetN(); ++i)
    {
        Ptr<Node> node = m_ueNodes.Get(i);
        // Orbiter already has an IP stack from SNS-3; installing twice aborts.
        if (!node->GetObject<Ipv4>())
        {
            internet.Install(node);
        }
    }
    m_ueIpIfaces = m_epcHelper->AssignUeIpv4Address(NetDeviceContainer(m_ueDevs));

    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    for (uint32_t u = 0; u < m_ueNodes.GetN(); ++u)
    {
        Ptr<Ipv4StaticRouting> ueStat =
            ipv4RoutingHelper.GetStaticRouting(m_ueNodes.Get(u)->GetObject<Ipv4>());
        ueStat->SetDefaultRoute(m_epcHelper->GetUeDefaultGatewayAddress(), 1);

        int served = (u < ues.size()) ? ues[u].servedEnbId : 0;
        if (served >= 0 && static_cast<uint32_t>(served) < m_enbDevs.GetN())
        {
            m_lteHelper->Attach(m_ueDevs.Get(u), m_enbDevs.Get(served));
        }
        else
        {
            m_lteHelper->Attach(m_ueDevs.Get(u));
        }
    }

    // Satellite feeder: PGW sits next to the SNS3 GW user.
    if (topology && topology->GetGwUserNodes().GetN() > 0)
    {
        Ptr<Node> pgw = m_epcHelper->GetPgwNode();
        Ptr<Node> gwUser = topology->GetGwUserNode(0);
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
        p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
        NetDeviceContainer pgwGwDevs = p2p.Install(pgw, gwUser);

        Ipv4AddressHelper ipv4;
        ipv4.SetBase("172.31.255.0", "255.255.255.0");
        Ipv4InterfaceContainer pgwGwIf = ipv4.Assign(pgwGwDevs);

        Ptr<Ipv4> gwIpv4 = gwUser->GetObject<Ipv4>();
        int32_t gwIfIndex = gwIpv4->GetInterfaceForDevice(pgwGwDevs.Get(1));
        Ptr<Ipv4StaticRouting> gwRt = ipv4RoutingHelper.GetStaticRouting(gwIpv4);
        gwRt->AddNetworkRouteTo(Ipv4Address("7.0.0.0"),
                                Ipv4Mask("255.0.0.0"),
                                pgwGwIf.GetAddress(0),
                                gwIfIndex);

        const uint16_t port = 5000;
        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApps = sink.Install(gwUser);
        sinkApps.Start(Seconds(0.0));
        sinkApps.Stop(simulationStop);

        Ipv4Address gwAddr = pgwGwIf.GetAddress(1);
        UdpClientHelper client(gwAddr, port);
        client.SetAttribute("MaxPackets", UintegerValue(0xffffffffu));
        client.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
        client.SetAttribute("PacketSize", UintegerValue(512));
        ApplicationContainer clientApps = client.Install(m_ueNodes);
        clientApps.Start(Seconds(1.0));
        clientApps.Stop(simulationStop);

        NS_LOG_UNCOND("[lte] Feeder: PGW " << pgwGwIf.GetAddress(0) << " <-> GW user " << gwAddr);
    }

    EnableLenaTraces();

    if (!outputDir.empty())
    {
        const std::string table = Join(outputDir, "lte-nodes.txt");
        std::ofstream out(table);
        out << "% enb_id mount carrier lat lon alt\n";
        for (const auto& rec : enbs)
        {
            out << rec.enbId << " " << rec.mount << " " << rec.carrierId << " " << rec.lat << " "
                << rec.lon << " " << rec.alt << "\n";
        }
        out << "% ue_id served_enb lat lon alt\n";
        for (const auto& rec : ues)
        {
            out << rec.ueId << " " << rec.servedEnbId << " " << rec.lat << " " << rec.lon << " "
                << rec.alt << "\n";
        }
    }

    return true;
}

} // namespace ns3
