# sibgu-hap

Модуль ns-3 (contrib) для моделирования **HAP** (High-Altitude Platform) совместно со спутниковым стеком **SNS-3** (`contrib/satellite`).  
Опционально подключает **LENA LTE** (eNB/UE) поверх той же сцены.

Автор: Igor Plastov (Reshetnev University) · репозиторий: [chetverovod/sibgu-hap](https://github.com/chetverovod/sibgu-hap).

## Возможности

- Сценарии SNS-3 с HAP-носителями (траектории, лучи, handover).
- Пример `hapsimulator` — основной entry point для пакетных прогонов.
- `LteCellularHelper` — установка LENA eNB/UE из `scenario/cellular/` (airborne-устройства делят mobility с HAP/orbiter; EPC к SNS-3 GW).
- `HandoverGuard` — защита от abort SNS-3 при потере покрытия (без правок satellite-модуля).
- `DynamicsSchedule` — применение расписания синтеза (beam stepping и др.) во время симуляции.
- Утилиты в `ext-utils/` — упаковка задач (`.tsk`), отчёты, генерация траекторий и DNT.

## Структура

| Путь | Назначение |
|------|------------|
| `model/` | Модель модуля, проверка траекторий орбитера |
| `helper/` | Helpers: LTE, handover guard, dynamics schedule, utils |
| `stats/` | IP-таблица устройств, PCAP-трассировка |
| `examples/` | Примеры симуляций (`hapsimulator`, handover, Wi‑Fi HAP и др.) |
| `test/` | Сюита `hapsimulator` |
| `data/scenarios/` | Сценарии SNS-3 / HAP |
| `ext-utils/` | Python-утилиты (см. [ext-utils/README.md](ext-utils/README.md)) |
| `doc/` | Заготовка Sphinx-документации |

## Зависимости

Модуль собирается внутри дерева **ns-3.43** и линкуется с:

- `core`, `network`, `internet`, `mobility`, `applications`, `point-to-point`
- `lte`
- `satellite` (SNS-3)

Модуль должен лежать в `contrib/sibgu-hap` относительно корня ns-3.

## Сборка

Из корня ns-3:

```bash
./ns3 configure --enable-examples --enable-tests
./ns3 build sibgu-hap
```

Примеры и тесты:

```bash
./ns3 build sibgu-hap-test
./ns3 build hapsimulator   # или имя цели примера после configure
```

## Запуск hapsimulator

Запускать **из корня ns-3** (пути к `contrib/sibgu-hap/data` относительные):

```bash
./ns3 run "hapsimulator --scenarioName=constellation-leo-3-satellites-hap --simulationDuration=2"
```

Полезные аргументы:

| Аргумент | Описание |
|----------|----------|
| `--scenarioName` | Имя каталога в `data/scenarios/` |
| `--simulationDuration` | Длительность, с |
| `--simulationStart` | Момент старта статистики / packet trace, с |
| `--packetSize` / `--interval` | CBR: размер (байт) и интервал (мс) |
| `--enableLte` | Включить LENA из `scenario/cellular/` |
| `--enablePcap` / `--enableHexDump` | PCAP / hex-dump |
| `--jdOrigin` | Julian date (UTC) нуля симуляции → `positions/start_date.txt` |

LTE включается флагом `--enableLte` или автоматически, если в сценарии есть `cellular/enb.conf` / `enb_positions.txt`.

## Сценарии

Каталог: `data/scenarios/`. Типичный состав: `antennapatterns/`, `beams/`, `positions/`, `standard/`, `waveforms/`, опционально `cellular/`, `beamhopping/`.

Примеры:

- `constellation-leo-3-satellites-hap`
- `constellation-leo-4-satellites-2-hap`
- `geo-33E-hap`
- `scen-test-hap`

Подробнее: [data/scenarios/README.md](data/scenarios/README.md).

## Тесты

Сюита ns-3: **`hapsimulator`**.

```bash
# быстрые unit-тесты
./ns3 run "test-runner --suite=hapsimulator --verbose"

# + интеграционный smoke (создание топологии SatHelper)
./ns3 run "test-runner --suite=hapsimulator --fullness=EXTENSIVE --verbose"
```

Покрывает: CLI-валидацию, парсинг cellular-конфигов, airborne trace mapping, пути LENA KPI, `HandoverGuard`, smoke-создание спутниковой топологии.

Тесты Python-утилит — из `ext-utils/` (см. README там).

## Helpers (кратко)

- **`LteCellularHelper`** — читает `scenario/cellular/`, ставит eNB/UE; airborne с `carrierId` живут на узле HAP/orbiter (общий `MobilityModel`, ECEF).
- **`HandoverGuard`** — периодически проверяет покрытие; при отсутствии валидного луча отключает handover-модуль на UT/GW.
- **`DynamicsSchedule`** — читает schedule и применяет события во время `Simulator::Run`.

## Лицензия

Код примеров и модуля распространяется на условиях **GPL-2.0** (как ns-3).
