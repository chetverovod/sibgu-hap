#include "ns3/test.h"
// Подключаем core-module для доступа к StringValue, BooleanValue, EnumValue и MilliSeconds
#include "ns3/core-module.h" 
// Специфичные хелперы спутникового модуля
#include "ns3/simulation-helper.h"
#include "ns3/satellite-helper.h"
#include "ns3/satellite-env-variables.h"
#include "ns3/satellite-topology.h"
#include "ns3/singleton.h"
#include "ns3/system-path.h"

#include <filesystem> // Для std::filesystem::remove_all
#include <string>

// Подключаем наши вынесенные утилиты (путь может отличаться в зависимости от вашей структуры)
#include "../helper/hapsimulator-utils.h"

using namespace ns3;

/**
 * \ingroup satellite
 * \brief Тест корректности фильтрации кастомных аргументов командной строки
 */
class FilterArgvTestCase : public TestCase
{
public:
    FilterArgvTestCase () : TestCase ("Test FilterArgvForCommandLine logic") {}
    virtual ~FilterArgvTestCase () {}

private:
    virtual void DoRun (void) override
    {
        std::string simsDir, scenarioPath;
        
        char arg0[] = "hapsimulator";
        char arg1[] = "--simsDir=/custom/sims/dir";
        char arg2[] = "--simulationDuration=10.5";
        char arg3[] = "--scenarioPath=/tmp/my_scenario";
        char arg4[] = "--packetSize=1024";
        char* argv[] = {arg0, arg1, arg2, arg3, arg4};
        int argc = 5;

        auto result = HapSimulatorUtils::FilterArgvForCommandLine(argc, argv, simsDir, scenarioPath);

        // Кастомные аргументы должны быть удалены из результирующего списка
        NS_TEST_ASSERT_MSG_EQ (result.size(), 3, "Filtered argv should contain exactly 3 standard arguments");
        NS_TEST_ASSERT_MSG_EQ (result[0], "hapsimulator", "Argv[0] should be program name");
        NS_TEST_ASSERT_MSG_EQ (result[1], "--simulationDuration=10.5", "Standard arg 1 kept");
        NS_TEST_ASSERT_MSG_EQ (result[2], "--packetSize=1024", "Standard arg 2 kept");
        
        // Кастомные переменные должны быть спарсены корректно
        NS_TEST_ASSERT_MSG_EQ (simsDir, "/custom/sims/dir", "simsDir parsed incorrectly");
        NS_TEST_ASSERT_MSG_EQ (scenarioPath, "/tmp/my_scenario", "scenarioPath parsed incorrectly");
    }
};

/**
 * \ingroup satellite
 * \brief Тест валидации входных параметров симуляции
 */
class ValidateCliInputsTestCase : public TestCase
{
public:
    ValidateCliInputsTestCase () : TestCase ("Test ValidateCliInputs logic") {}
    virtual ~ValidateCliInputsTestCase () {}

private:
    virtual void DoRun (void) override
    {
        std::vector<std::string> issues;

        // Тест 1: Все параметры невалидны
        HapSimulatorUtils::ValidateCliInputs(-1.0, 0.0, 0, "", issues);
        NS_TEST_ASSERT_MSG_EQ (issues.size(), 4, "Should detect 4 invalid parameters");
        issues.clear();

        // Тест 2: Все параметры валидны
        HapSimulatorUtils::ValidateCliInputs(10.0, 50.0, 1024, "/some/dir", issues);
        NS_TEST_ASSERT_MSG_EQ (issues.size(), 0, "Valid parameters should produce 0 issues");
        issues.clear();

        // Тест 3: Граничные условия (нулевые значения)
        HapSimulatorUtils::ValidateCliInputs(0.0, 100.0, 1024, "/dir", issues);
        NS_TEST_ASSERT_MSG_EQ (issues.size(), 1, "Zero duration should trigger 1 issue");
    }
};

/**
 * \ingroup satellite
 * \brief Интеграционный тест на создание спутниковой топологии (Smoke Test)
 */
class TopologyCreationSmokeTestCase : public TestCase
{
public:
    TopologyCreationSmokeTestCase () : TestCase ("Smoke test for SatHelper topology creation") {}
    virtual ~TopologyCreationSmokeTestCase () {}

private:
    virtual void DoRun (void) override
    {
        // 1. Подготавливаем минимальную конфигурацию
        Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode", EnumValue(SatEnums::REGENERATION_NETWORK));
        Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode", EnumValue(SatEnums::REGENERATION_NETWORK));
        Config::SetDefault("ns3::SatGwMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
        Config::SetDefault("ns3::SatOrbiterMac::DisableSchedulingIfNoDeviceConnected", BooleanValue(true));
        Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));

        // Указываем путь к данным
        Config::SetDefault("ns3::SatEnvVariables::DataPath", StringValue(HapSimulatorUtils::kHapDataRoot));

        std::string scenarioName = "constellation-leo-3-satellites-hap";
        
        // Создаем уникальную временную директорию
        std::string outputDir = SystemPath::MakeTemporaryDirectoryName();
        std::filesystem::create_directories(outputDir);

        // 2. Проверяем валидность файловой структуры перед запуском хелпера
        std::string scenarioRoot = std::string(HapSimulatorUtils::kHapDataRoot) + "/scenarios/" + scenarioName;
        std::vector<std::string> layoutIssues;
        HapSimulatorUtils::ValidateScenarioLayout(scenarioRoot, layoutIssues);
        
        if (!layoutIssues.empty())
        {
            // Если файлов нет, просто пропускаем тест
            NS_TEST_ASSERT_MSG_EQ (true, true, "Scenario data files missing, skipping topology creation test.");
            std::filesystem::remove_all(outputDir);
            return;
        }

        // 3. Создаем хелпер и пробуем поднять топологию
        Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("test-hap-smoke");
        simulationHelper->SetOutputPath(outputDir);
        simulationHelper->SetSimulationTime(MilliSeconds(10)); // 10мс виртуального времени
        simulationHelper->SetGwUserCount(1);
        simulationHelper->SetUserCountPerUt(1);
        
        // Используем только 1 луч для скорости
        std::set<uint32_t> singleBeam = {1};
        simulationHelper->SetBeamSet(singleBeam);

        try
        {
            simulationHelper->LoadScenario(scenarioName);
            simulationHelper->CreateSatScenario(SatHelper::NONE);

            // 4. Проверяем, что узлы создались
            Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
            NS_TEST_ASSERT_MSG_GT (topology->GetGwNodes().GetN(), 0, "No GW nodes created!");
            NS_TEST_ASSERT_MSG_GT (topology->GetOrbiterNodes().GetN(), 0, "No SAT nodes created!");
            NS_TEST_ASSERT_MSG_GT (topology->GetUtNodes().GetN(), 0, "No UT nodes created!");
        }
        
        catch (const std::exception& e)
        {
            // Идиоматичный способ завалить тест в ns-3 с кастомным сообщением
            NS_TEST_ASSERT_MSG_EQ (false, true, std::string("SatHelper threw an exception: ") + e.what());
        }

        // 5. Очистка временной директории средствами C++17
        std::filesystem::remove_all(outputDir);
    }
};

/**
 * \ingroup satellite
 * \brief Тестовый сьют, объединяющий все тесты hapsimulator (с учетом синтаксиса ns-3.43)
 */
class HapSimulatorTestSuite : public TestSuite
{
public:
    // Используем TestSuite::Type::UNIT вместо устаревшего UNIT
    HapSimulatorTestSuite () : TestSuite ("hapsimulator", TestSuite::Type::UNIT)
    {
        // Используем TestCase::Duration::QUICK вместо устаревшего TestCase::QUICK
        AddTestCase (new FilterArgvTestCase, TestCase::Duration::QUICK);
        AddTestCase (new ValidateCliInputsTestCase, TestCase::Duration::QUICK);
        
        // Используем TestCase::Duration::EXTENSIVE для интеграционного теста
        AddTestCase (new TopologyCreationSmokeTestCase, TestCase::Duration::EXTENSIVE);
    }
};

// Статическая инициализация сьюта
static HapSimulatorTestSuite sHapSimulatorTestSuite;