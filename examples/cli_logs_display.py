#!/usr/bin/env python3

# Copyright (c) 2018
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation;
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Adapted CLI Viewer for generic PacketTrace
#

import os
import sys
import argparse
import bisect
import re

import cli_logs_parser

# ANSI цвета для консоли
class Colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    
    # Цвета для событий
    SND = '\033[32m'  # Green (Send)
    RCV = '\033[34m'  # Blue (Receive)
    DRP = '\033[31m'  # Red (Drop)
    ENQ = '\033[33m'  # Yellow (Enqueue)
    OTHER = '\033[37m' # White

    # Цвета для направлений
    FWD = '\033[36m'  # Cyan
    RTN = '\033[35m'  # Magenta

    # Общие цвета
    RED = '\033[31m' 
    GREEN = '\033[32m'
    CYAN = '\033[36m'

def get_color_for_event(event):
    if event == 'SND': return Colors.SND
    if event == 'RCV': return Colors.RCV
    if event == 'DRP': return Colors.DRP
    if event == 'ENQ': return Colors.ENQ
    return Colors.OTHER

def get_color_for_direction(direction):
    if direction == 'FWD': return Colors.FWD
    if direction == 'RTN': return Colors.RTN
    return Colors.RESET

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def get_unique_devices(entries):
    """Собирает уникальные устройства из лога."""
    devices = set()
    for e in entries:
        devices.add((e.node_type, e.node_id))
    # Сортируем: сначала по типу, потом по ID
    return sorted(list(devices), key=lambda x: (x[0], x[1]))

def show_device_list(devices):
    """Отображает список устройств на отдельном экране."""
    clear_screen()
    print(f"{Colors.BOLD}List of Devices Found in Log:{Colors.RESET}")
    print("-" * 30)
    if not devices:
        print("No devices found.")
    else:
        # Форматный вывод
        print(f"{'Type':<5} | {'ID':<5}")
        print("-" * 15)
        for dtype, did in devices:
            print(f"{dtype:<5} | {did:<5}")
    print("\nPress Enter to return to viewer...")
    input()

def format_entry(entry, highlight=False):
    """Форматирует одну запись лога в строку таблицы."""
    time_str = f"{entry.time:.6f}"
    
    event_color = get_color_for_event(entry.event)
    dir_color = get_color_for_direction(entry.direction)
    
    # Если запись является результатом поиска, инвертируем цвета для подсветки
    if highlight:
        event_color = '\033[7m' # Invert background/foreground
        dir_color = '\033[7m'

    # Формирование информации о MAC адресах (если есть)
    mac_info = ""
    if entry.src_mac and entry.dst_mac:
        mac_info = f"{entry.src_mac} -> {entry.dst_mac}"
    elif entry.src_mac:
        mac_info = f"Src: {entry.src_mac}"
    
    # Форматирование строки с фиксированной шириной колонок для читаемости
    line = (
        f"{time_str:<10} "
        f"{event_color}{entry.event:<3}{Colors.RESET} "
        f"{entry.node_type:<3} "
        f"{str(entry.node_id):<3} "
        f"{entry.mac:<17} "
        f"{entry.level:<3} "
        f"{dir_color}{entry.direction:<3}{Colors.RESET} "
        f"{str(entry.packet_id):<5} "
        f"{mac_info}"
    )
    return line

def print_header():
    """Выводит заголовок таблицы."""
    header = (
        f"{'Time':<10} "
        f"{'Evt':<3} "
        f"{'Typ':<3} "
        f"{'ID':<3} "
        f"{'MAC Address':<17} "
        f"{'Lvl':<3} "
        f"{'Dir':<3} "
        f"{'Pkt':<5} "
        f"{'Source -> Destination'}"
    )
    print(f"{Colors.BOLD}{header}{Colors.RESET}")
    print("-" * 100)

def run_display(log_filename, page_size=20):
    """
    Основной цикл отображения.
    """
    print(f"Loading trace: {log_filename}...")
    entries = list(cli_logs_parser.read_trace_file(log_filename))
    total_entries = len(entries)
    
    if total_entries == 0:
        print("No valid trace entries found.")
        return

    # Создаем список времен для быстрого поиска (бинарный поиск)
    entry_times = [e.time for e in entries]

    print(f"Loaded {total_entries} entries.")

    current_index = 0
    
    # Переменные для поиска (Search Mode)
    search_indices = []
    current_search_pos = -1
    search_term = ""

    # Переменные для фильтрации (Filter Mode)
    # filter_mode может быть: None, 'packet', 'device'
    filter_mode = None 
    filter_description = ""
    filtered_indices = []
    filtered_target_id = None 

    while True:
        clear_screen()
        
        # --- Логика определения текущего режима и границ ---
        display_entries = []
        display_start = 0
        display_end = 0
        
        if filter_mode is not None:
            # Режим фильтрации (пакет или устройство)
            total_viewable = len(filtered_indices)
            
            # Защита от выхода за границы списка
            if current_index >= total_viewable:
                current_index = max(0, total_viewable - 1)
            elif current_index < 0:
                current_index = 0

            display_start = current_index
            display_end = min(current_index + page_size, total_viewable)
            
            # Получаем реальные индексы из общего списка
            real_indices = filtered_indices[display_start:display_end]
            display_entries = [entries[i] for i in real_indices]
            
            if filter_mode == 'packet':
                status_suffix = f" | {Colors.BOLD}{Colors.RED}FILTERED ON PKT ID: {filtered_target_id}{Colors.RESET} ({total_viewable} events)"
            else: # device
                status_suffix = f" | {Colors.BOLD}{Colors.RED}FILTERED ON DEV: {filter_description}{Colors.RESET} ({total_viewable} events)"
        else:
            # Обычный режим
            if current_index >= total_entries:
                current_index = total_entries - 1
            elif current_index < 0:
                current_index = 0
                
            display_start = current_index
            display_end = min(current_index + page_size, total_entries)
            display_entries = entries[display_start:display_end]
            total_viewable = total_entries
            
            status_suffix = ""
            if search_indices:
                status_suffix += f" | Search: '{search_term}' [{current_search_pos + 1}/{len(search_indices)}]"

        # --- Вывод на экран ---
        status_msg = f"CLI Packet Trace Viewer | Page {display_start // page_size + 1}/{(total_viewable - 1) // page_size + 1}"
        status_msg += status_suffix
        
        print(f"{Colors.BOLD}{status_msg}{Colors.RESET}")
        print_header()
        
        # Вывод строк
        for i, entry in enumerate(display_entries):
            is_match = False
            # Подсветка поиска работает только если не в режиме фильтрации
            if filter_mode is None and search_indices and current_search_pos >= 0 and current_search_pos < len(search_indices):
                # real_idx - это индекс в основном массиве entries
                real_idx = display_start + i
                if real_idx == search_indices[current_search_pos]:
                    is_match = True
            
            print(format_entry(entry, highlight=is_match))
        
        print(f"\nShowing entries {display_start + 1} - {display_end} of {total_viewable}")
        
        # --- Подсказка управления (разбита на 2 строки) ---
        print(f"{Colors.BOLD}Controls:{Colors.RESET} [Enter] Next | [b] Back | [t <time>] Jump | [l] List Devs | [Q] Quit")
        
        if filter_mode is not None:
            print(f"{Colors.BOLD}         {Colors.RESET} [c] Clear Filter")
        else:
            print(f"{Colors.BOLD}         {Colors.RESET} [s <mac>] Search MAC | [p <id>] Search Pkt | [f <id>] Filter Pkt |")
            print(f"{Colors.BOLD}         {Colors.RESET} [d <type1><id1> [| <type2><id2> |...]] Filter Devs | [n/N] Next/Prev")

        # --- Обработка ввода ---
        try:
            user_input = input("> ").strip()
            
            if user_input.lower() == 'q':
                break
            
            # --- Команды, работающие в обоих режимах ---
            elif not user_input:
                current_index += page_size
            elif user_input.lower() == 'b':
                current_index = max(0, current_index - page_size)
            
            # --- Команды режима Фильтрации ---
            elif user_input.lower() == 'c':
                if filter_mode is not None:
                    # Возвращаемся к полному логу, пытаясь сохранить позицию по времени
                    try:
                        current_real_idx = filtered_indices[current_index]
                        current_time = entries[current_real_idx].time
                        filter_mode = None
                        filtered_indices = []
                        idx = bisect.bisect_left(entry_times, current_time)
                        current_index = idx
                    except:
                        filter_mode = None
                        filtered_indices = []
                        current_index = 0
                else:
                    print("Not in filter mode.")
                    input()
            
            # --- Команды только для Обычного режима ---
            elif filter_mode is None:
                # Команда вывода списка устройств (НОВОЕ)
                if user_input.lower() == 'l':
                    devices = get_unique_devices(entries)
                    show_device_list(devices)

                # Поиск по MAC
                elif user_input.lower().startswith('s '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        search_term = f"MAC: {parts[1]}"
                        term_lower = parts[1].lower()
                        search_indices = [
                            i for i, e in enumerate(entries) 
                            if term_lower in e.mac.lower() or 
                               (e.src_mac and term_lower in e.src_mac.lower()) or 
                               (e.dst_mac and term_lower in e.dst_mac.lower())
                        ]
                        current_search_pos = -1
                        if search_indices:
                            current_search_pos = 0
                            current_index = search_indices[0]
                        else:
                            print(f"No matches found for MAC '{parts[1]}'. Press Enter...")
                            input() 
                    else:
                        print("Usage: s <mac_address>")
                        input()

                # Фильтрация по ID пакета
                elif user_input.lower().startswith('f '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_pid = int(parts[1])
                            filtered_target_id = target_pid
                            filtered_indices = [
                                i for i, e in enumerate(entries) 
                                if e.packet_id == target_pid
                            ]
                            if filtered_indices:
                                filter_mode = 'packet'
                                current_index = 0
                                # Сбрасываем поиск
                                search_indices = []
                                search_term = ""
                            else:
                                print(f"No events found for Packet ID '{target_pid}'. Press Enter...")
                                input()
                        except ValueError:
                            print("Invalid Packet ID. Use integer (e.g., 42)")
                            input()
                    else:
                        print("Usage: f <packet_id>")
                        input()

                # Поиск по ID пакета (без фильтрации)
                elif user_input.lower().startswith('p '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_pid = int(parts[1])
                            search_term = f"Packet ID: {target_pid}"
                            search_indices = [
                                i for i, e in enumerate(entries) 
                                if e.packet_id == target_pid
                            ]
                            current_search_pos = -1
                            if search_indices:
                                current_search_pos = 0
                                current_index = search_indices[0]
                            else:
                                print(f"No matches found for Packet ID '{target_pid}'. Press Enter...")
                                input()
                        except ValueError:
                            print("Invalid Packet ID. Use integer (e.g., 42)")
                            input()
                    else:
                        print("Usage: p <packet_id>")
                        input()

                # Фильтрация по Устройствам (Обновлено для поддержки нескольких)
                elif user_input.lower().startswith('d '):
                    # Получаем аргументы после 'd '
                    args_str = user_input[2:].strip()
                    
                    if not args_str:
                        print("Usage: d <type><id> | <type><id> ... (e.g., d SAT0 | UT1)")
                        input()
                    else:
                        # Разбиваем строку по символу '|'
                        raw_parts = args_str.split('|')
                        target_devices = []
                        valid_input = True
                        
                        for part in raw_parts:
                            clean_part = part.strip()
                            # Парсинг строки вида "SAT1", "UT10", "GW0"
                            match = re.match(r'^([A-Z]+)(\d+)$', clean_part)
                            if match:
                                target_type = match.group(1)
                                target_id = int(match.group(2))
                                target_devices.append((target_type, target_id))
                            else:
                                print(f"Invalid device format: '{clean_part}'. Expected 'TypeID' (e.g., SAT0).")
                                valid_input = False
                                break
                        
                        if valid_input and target_devices:
                            # Формируем описание для статуса
                            desc_parts = [f"{t}{i}" for t, i in target_devices]
                            filter_description = ", ".join(desc_parts)
                            
                            # Фильтруем
                            filtered_indices = [
                                i for i, e in enumerate(entries) 
                                if (e.node_type, e.node_id) in target_devices
                            ]
                            
                            if filtered_indices:
                                filter_mode = 'device'
                                filtered_target_id = len(target_devices) # Просто счетчик для инфо
                                current_index = 0
                                search_indices = []
                                search_term = ""
                            else:
                                print(f"No events found for devices: {filter_description}. Press Enter...")
                                input()
                        elif valid_input:
                             print("No valid devices provided.")
                             input()
                        else:
                            input()

                # Навигация по результатам поиска
                elif user_input.lower() == 'n':
                    if search_indices:
                        if current_search_pos + 1 < len(search_indices):
                            current_search_pos += 1
                            current_index = search_indices[current_search_pos]
                        else:
                            print("End of search results.")
                            input()
                    else:
                        print("No active search. Use 's <mac>' or 'p <id>' first.")
                        input()
                
                elif user_input == 'N':
                    if search_indices:
                        if current_search_pos > 0:
                            current_search_pos -= 1
                            current_index = search_indices[current_search_pos]
                        else:
                            print("Start of search results.")
                            input()
                    else:
                        print("No active search. Use 's <mac>' or 'p <id>' first.")
                        input()

                # Переход по времени
                elif user_input.lower().startswith('t '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_time = float(parts[1])
                            idx = bisect.bisect_left(entry_times, target_time)
                            if idx < total_entries:
                                current_index = idx
                            else:
                                current_index = total_entries - 1
                        except ValueError:
                            print("Invalid time format. Use float (e.g., 1.5)")
                            input()
                    else:
                        print("Usage: t <time>")
                        input()
            
            else:
                print("Command not available in Filter Mode. Press [c] to exit filter first.")
                input()

        except (EOFError, KeyboardInterrupt):
            break

    print(f"\n{Colors.BOLD}End of trace.{Colors.RESET}")

def main():
    parser = argparse.ArgumentParser(description='Display Satellite Packet Trace')
    parser.add_argument('log_file', type=str, help='Path to PacketTrace.log file')
    parser.add_argument('--page-size', type=int, default=20, help='Number of lines per page')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.log_file):
        print(f"Error: File '{args.log_file}' not found.")
        sys.exit(1)

    run_display(args.log_file, args.page_size)

if __name__ == '__main__':
    main()