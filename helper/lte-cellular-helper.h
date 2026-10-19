#ifndef SIBGU_HAP_LTE_CELLULAR_HELPER_H
#define SIBGU_HAP_LTE_CELLULAR_HELPER_H

#include "ns3/ipv4-interface-container.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ns3
{

class LteHelper;
class PointToPointEpcHelper;
class SatTopology;
class Node;

struct LteEnbRecord
{
    uint32_t enbId{0};
    uint32_t cellId{1};
    uint32_t earfcn{100};
    double bandwidthMhz{20.0};
    double txPowerDbm{46.0};
    std::string mount{"ground"};
    std::string carrierId{"-"};
    double lat{0.0};
    double lon{0.0};
    double alt{0.0};
    std::string traceRelPath;
};

struct LteUeRecord
{
    uint32_t ueId{0};
    int servedEnbId{-1};
    std::string mount{"ground"};
    std::string carrierId{"-"};
    double lat{0.0};
    double lon{0.0};
    double alt{1.5};
    std::string traceRelPath;
};

/**
 * \brief Reads scenario/cellular/ and installs LENA eNB/UE on the satellite scene.
 *
 * Airborne eNB/UE with a carrier are installed on that SNS-3 orbiter node
 * (shared MobilityModel). SatTracedMobilityModel::DoGetPosition() returns ECEF
 * so LENA Friis matches SNS-3 GetGeoPosition(). Ground devices without a carrier
 * stay on a dedicated node with ECEF waypoints.
 *
 * EPC/PGW is attached to the first SNS3 GW user (satellite feeder). SNS3 UT nodes
 * are never converted into LTE UE.
 */
class LteCellularHelper
{
  public:
    LteCellularHelper();
    ~LteCellularHelper();
    LteCellularHelper(const LteCellularHelper&) = delete;
    LteCellularHelper& operator=(const LteCellularHelper&) = delete;

    static bool HasCellularScenario(const std::string& scenarioRoot);
    static void EnsureUtPositionsFile(const std::string& scenarioRoot);

    static uint16_t MhzToResourceBlocks(double bandwidthMhz);
    static uint32_t UlEarfcnFromDl(uint32_t dlEarfcn);

    static std::vector<LteEnbRecord> LoadEnbs(const std::string& scenarioRoot);
    static std::vector<LteUeRecord> LoadUes(const std::string& scenarioRoot);

    bool Install(const std::string& scenarioRoot,
                 Ptr<SatTopology> topology,
                 Time simulationStop,
                 const std::string& outputDir);

    NodeContainer GetEnbNodes() const
    {
        return m_enbNodes;
    }

    NodeContainer GetUeNodes() const
    {
        return m_ueNodes;
    }

    Ptr<LteHelper> GetLteHelper() const;

    /**
     * Point LENA KPI files (DlRsrpSinrStats.txt and related) into outputDir
     * via Config::SetDefault. Must run before LteHelper is created so PHY/MAC
     * calculators pick up the paths. Empty outputDir leaves LENA defaults (cwd).
     */
    static void ConfigureLenaTraceOutput(const std::string& outputDir);

  private:
    void EnableLenaTraces();
    static bool SkipComment(const std::string& line);
    static std::string Basename(const std::string& path);
    static std::string Join(const std::string& root, const std::string& rel);
    static bool FileExists(const std::string& path);
    static std::string ResolveTracePath(const std::string& scenarioRoot, const std::string& rel);
    static std::optional<uint32_t> FindOrbiterSatId(const std::string& scenarioRoot,
                                                    const std::string& mount,
                                                    const std::string& carrierId,
                                                    const std::string& traceRelPath);
    static Ptr<Node> PlaceOnCarrier(const std::string& scenarioRoot,
                                    Ptr<SatTopology> topology,
                                    const std::string& mount,
                                    const std::string& carrierId,
                                    const std::string& traceRelPath,
                                    double lat,
                                    double lon,
                                    double alt);
    static void InstallGeoMobility(Ptr<Node> node,
                                   double lat,
                                   double lon,
                                   double alt,
                                   const std::string& tracePath);

    NodeContainer m_enbNodes;
    NodeContainer m_ueNodes;
    NetDeviceContainer m_enbDevs;
    NetDeviceContainer m_ueDevs;
    Ipv4InterfaceContainer m_ueIpIfaces;
    Ptr<LteHelper> m_lteHelper;
    Ptr<PointToPointEpcHelper> m_epcHelper;
};

} // namespace ns3

#endif /* SIBGU_HAP_LTE_CELLULAR_HELPER_H */
