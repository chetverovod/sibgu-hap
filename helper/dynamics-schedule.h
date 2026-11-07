#ifndef SIBGU_HAP_DYNAMICS_SCHEDULE_H
#define SIBGU_HAP_DYNAMICS_SCHEDULE_H

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ns3
{

class SatAntennaGainPatternContainer;

/**
 * \ingroup sibgu-hap
 * One row of scenario/dynamics/schedule.txt.
 *
 * lat/lon move the antenna-pattern peak. freq/bw are the synthesis carrier
 * at that instant; SNS3 keeps the scenario channel plan, and the values are
 * recorded in the applied log.
 */
struct DynamicsEvent
{
    double timeS{0.0};
    uint32_t beamId{0};
    bool hasPointing{false};
    double latDeg{0.0};
    double lonDeg{0.0};
    bool hasCarrier{false};
    double freqHz{0.0};
    double bandwidthHz{0.0};
};

struct DynamicsBeamRef
{
    double latDeg{0.0};
    double lonDeg{0.0};
    std::string name;
};

/**
 * \ingroup sibgu-hap
 * \brief Read the synthesis schedule and apply it while the simulation runs.
 *
 * The file is optional. A scene without dynamics keeps the static snapshot
 * already written into the antenna patterns and beam configuration.
 */
class DynamicsSchedule : public Object
{
  public:
    static TypeId GetTypeId();

    DynamicsSchedule();
    ~DynamicsSchedule() override = default;

    /**
     * \brief Parse schedule.txt. False when the file is missing or has no events.
     */
    bool LoadFile(const std::string& path);

    const std::vector<DynamicsEvent>& Events() const;
    const DynamicsBeamRef* FindBeam(uint32_t beamId) const;

    /**
     * \brief Schedule each event on Simulator. patterns may be null in tests.
     * \param logPath Applied rows are appended here; empty skips the file.
     */
    void Install(Ptr<SatAntennaGainPatternContainer> patterns, const std::string& logPath);

    uint32_t AppliedCount() const;

  private:
    void DoDispose() override;
    void Apply(DynamicsEvent event);
    static bool ParseDouble(const std::string& token, double& value);

    std::vector<DynamicsEvent> m_events;
    std::unordered_map<uint32_t, DynamicsBeamRef> m_beams;
    Ptr<SatAntennaGainPatternContainer> m_patterns;
    std::ofstream m_log;
    uint32_t m_applied{0};
};

} // namespace ns3

#endif /* SIBGU_HAP_DYNAMICS_SCHEDULE_H */
