#ifndef SIBGU_HAP_HANDOVER_GUARD_H
#define SIBGU_HAP_HANDOVER_GUARD_H

#include "ns3/event-id.h"
#include "ns3/geo-coordinate.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ns3
{

class MobilityModel;
class Node;
class SatAntennaGainPatternContainer;
class SatMac;
class SatTopology;

/**
 * \ingroup sibgu-hap
 * \brief Prevents SNS-3 abort when handover has nowhere to go.
 *
 * SNS-3 is not modified. When GetBestBeamId(..., ignoreNan=true) returns 0 for
 * every orbiter, SatGwMac::StartTransmission would still attempt a handover to
 * beam 0 and dereference a null scheduler. This guard polls coverage and calls
 * SatMac::SetHandoverModule(nullptr) before that path runs, then logs once.
 *
 * SGP4 mobility does not emit CourseChange while propagating, so a periodic
 * poll is the primary trigger. Interval is 1 ms so the check runs before the
 * next DVB-S2 BB-frame StartTransmission.
 */
class HandoverGuard : public Object
{
  public:
    static TypeId GetTypeId();

    HandoverGuard();
    ~HandoverGuard() override = default;

    /**
     * \brief Watch UT and GW nodes and disarm handover when no valid beam remains.
     * \param topology Satellite topology after CreateSatScenario
     * \param patterns Antenna gain patterns from SatHelper::GetAntennaGainPatterns
     */
    void Install(Ptr<SatTopology> topology, Ptr<SatAntennaGainPatternContainer> patterns);

  private:
    void DoDispose() override;
    void NotifyCourseChange(Ptr<const MobilityModel> model);
    void CheckCoverage();
    void CheckNode(Ptr<Node> node, const std::string& role);
    bool CurrentBeamHasGain(Ptr<Node> node, const GeoCoordinate& coords);
    bool HasAnyValidBeam(const GeoCoordinate& coords);
    void DisarmHandover(Ptr<Node> node, const std::string& role);
    static void CollectSatMacs(Ptr<Node> node, std::vector<Ptr<SatMac>>& macs);
    static void CurrentSatAndBeam(Ptr<SatMac> mac, uint32_t& satId, uint32_t& beamId);

    Ptr<SatAntennaGainPatternContainer> m_patterns;
    std::vector<Ptr<Node>> m_groundNodes;
    std::vector<std::string> m_groundRoles;
    uint32_t m_nOrbiters{0};
    std::set<uint32_t> m_disarmedNodeIds;
    EventId m_event;
    Time m_interval;
    uint32_t m_armedCount{0};
};

} // namespace ns3

#endif /* SIBGU_HAP_HANDOVER_GUARD_H */
