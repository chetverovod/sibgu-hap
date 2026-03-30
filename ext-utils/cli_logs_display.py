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
    return sorted(list(devices), key=lambda x: (x[0], x[1]))

def show_device_list(devices):
    """Отображает список устройств на отдельном экране."""
    clear_screen()
    print(f"{Colors.BOLD}List of Devices Found in Log:{Colors.RESET}")
    print("-" * 30)
    if not devices:
        print("No devices found.")
    else:
        print(f"{'Type':<5} | {'ID':<5}")
        print("-" * 15)
        for dtype, did in devices:
            print(f"{dtype:<5} | {did:<5}")
    print("\nPress Enter to return to viewer...")
    input()

def build_mac_map(entries):
    """
    Строит словарь MAC адрес -> (Type, ID).
    ВАЖНО: Мы используем поле 'mac' (собственный адрес интерфейса устройства),
    которое есть в каждой строке лога, а также 'src_mac'.
    Это гарантирует, что устройства, которые только принимают пакеты, тоже будут найдены.
    """
    mac_map = {}
    for e in entries:
        # 1. Основной MAC интерфейса устройства (колонка 'MAC Address' в логе)
        # Он присутствует всегда, когда устройство участвует в событии.
        if e.mac:
            mac_map[e.mac] = (e.node_type, e.node_id)
        
        # 2. Исходящий MAC (пакет от этого устройства)
        # Если он отличается от основного (редкость), тоже добавляем.
        if e.src_mac:
            mac_map[e.src_mac] = (e.node_type, e.node_id)
            
    return mac_map

def show_device_interactions(entries, mac_map, target_type, target_id):
    """
    Показывает список устройств, с которыми взаимодействовал указанный узел.
    """
    peers = set()
    broadcast_found = False
    unknown_found = False

    target_device = (target_type, target_id)

    for e in entries:
        # Определяем реального отправителя через MAC
        src_dev = mac_map.get(e.src_mac)
        src_t, src_id = src_dev if src_dev else (None, None)

        # Определяем реального получателя через MAC
        dst_dev = mac_map.get(e.dst_mac)
        dst_t, dst_id = dst_dev if dst_dev else (None, None)

        # Отслеживаем Broadcast и Unknown
        if e.dst_mac:
            if e.dst_mac.lower() == "ff:ff:ff:ff:ff:ff":
                broadcast_found = True
            elif dst_dev is None:
                unknown_found = True

        # Логика поиска взаимодействий:
        # 1. Если целевое устройство - ИСТОЧНИК пакета, добавляем ПОЛУЧАТЕЛЯ.
        if (src_t, src_id) == target_device:
            if dst_dev:
                peers.add(dst_dev)
        
        # 2. Если целевое устройство - ПОЛУЧАТЕЛЬ пакета, добавляем ИСТОЧНИКА.
        if (dst_t, dst_id) == target_device:
            if src_dev:
                peers.add(src_dev)

    # Удаляем самого себя из списка (на случай loopback взаимодействий)
    peers.discard(target_device)

    clear_screen()
    print(f"{Colors.BOLD}Interactions for Device: {target_type}{target_id}{Colors.RESET}")
    print("-" * 40)
    
    if not peers:
        print("No direct interactions found with specific devices.")
    
    # Сортировка и вывод
    sorted_peers = sorted(list(peers), key=lambda x: (x[0], x[1]))
    for p_type, p_id in sorted_peers:
        print(f"  {p_type:<5} | {p_id:<5}")

    # Дополнительная информация
    footer_lines = []
    if broadcast_found:
        footer_lines.append(f"{Colors.CYAN}* Communicated via BROADCAST{Colors.RESET}")
    if unknown_found:
        footer_lines.append(f"{Colors.RED}* Communicated with UNKNOWN devices (MAC not in map){Colors.RESET}")
    
    if footer_lines:
        print("\n".join(footer_lines))
            
    print("\nPress Enter to return...")
    input()

def format_entry(entry, mac_map, highlight=False):
    """Форматирует одну запись лога в строку таблицы."""
    time_str = f"{entry.time:.6f}"
    
    event_color = get_color_for_event(entry.event)
    dir_color = get_color_for_direction(entry.direction)
    
    if highlight:
        event_color = '\033[7m'
        dir_color = '\033[7m'

    dest_type = "-"
    dest_id = "-"
    
    if entry.dst_mac:
        if entry.dst_mac in mac_map:
            dest_type, dest_id = mac_map[entry.dst_mac]
        elif entry.dst_mac.lower() == "ff:ff:ff:ff:ff:ff":
            dest_type = "BRD"
            dest_id = "-"
        else:
            dest_type = "UNK"
            dest_id = "-"

    mac_info = ""
    if entry.src_mac and entry.dst_mac:
        mac_info = f"{entry.src_mac} -> {entry.dst_mac}"
    elif entry.src_mac:
        mac_info = f"Src: {entry.src_mac}"
    
    line = (
        f"{time_str:<10} "
        f"{event_color}{entry.event:<3}{Colors.RESET} "
        f"{entry.node_type:<3} "
        f"{str(entry.node_id):<3} "
        f"{dest_type:<7} "
        f"{str(dest_id):<6} "
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
        f"{'DestTyp':<7} "
        f"{'DestID':<6} "
        f"{'Lvl':<3} "
        f"{'Dir':<3} "
        f"{'Pkt':<5} "
        f"{'Source MAC -> Destination MAC'}"
    )
    print(f"{Colors.BOLD}{header}{Colors.RESET}")
    print("-" * 93)

def run_display(log_filename, page_size=20):
    print(f"Loading trace: {log_filename}...")
    entries = list(cli_logs_parser.read_trace_file(log_filename))
    total_entries = len(entries)
    
    if total_entries == 0:
        print("No valid trace entries found.")
        return

    entry_times = [e.time for e in entries]
    mac_to_device_map = build_mac_map(entries)

    print(f"Loaded {total_entries} entries.")

    current_index = 0
    search_indices = []
    current_search_pos = -1
    search_term = ""
    filter_mode = None 
    filter_description = ""
    filtered_indices = []
    filtered_target_id = None 

    while True:
        clear_screen()
        display_entries = []
        display_start = 0
        display_end = 0
        
        if filter_mode is not None:
            total_viewable = len(filtered_indices)
            if current_index >= total_viewable:
                current_index = max(0, total_viewable - 1)
            elif current_index < 0:
                current_index = 0
            display_start = current_index
            display_end = min(current_index + page_size, total_viewable)
            real_indices = filtered_indices[display_start:display_end]
            display_entries = [entries[i] for i in real_indices]
            if filter_mode == 'packet':
                status_suffix = f" | {Colors.BOLD}{Colors.RED}FILTERED ON PKT ID: {filtered_target_id}{Colors.RESET} ({total_viewable} events)"
            else: 
                status_suffix = f" | {Colors.BOLD}{Colors.RED}FILTERED ON DEV: {filter_description}{Colors.RESET} ({total_viewable} events)"
        else:
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

        status_msg = f"CLI Packet Trace Viewer | Page {display_start // page_size + 1}/{(total_viewable - 1) // page_size + 1}"
        status_msg += status_suffix
        print(f"{Colors.BOLD}{status_msg}{Colors.RESET}")
        print_header()
        
        for i, entry in enumerate(display_entries):
            is_match = False
            if filter_mode is None and search_indices and current_search_pos >= 0 and current_search_pos < len(search_indices):
                real_idx = display_start + i
                if real_idx == search_indices[current_search_pos]:
                    is_match = True
            print(format_entry(entry, mac_to_device_map, highlight=is_match))
        
        print(f"\nShowing entries {display_start + 1} - {display_end} of {total_viewable}")
        print(f"{Colors.BOLD}Controls:{Colors.RESET} [Enter] Next | [b] Back | [t <time>] Jump | [l] List Devs | [i <type><id>] Interactions | [Q] Quit")
        
        if filter_mode is not None:
            print(f"{Colors.BOLD}         {Colors.RESET} [c] Clear Filter")
        else:
            print(f"{Colors.BOLD}         {Colors.RESET} [s <mac>] Search MAC | [p <id>] Search Pkt | [f <id>] Filter Pkt |")
            print(f"{Colors.BOLD}         {Colors.RESET} [d <type1><id1> [| <type2><id2> |...]] Filter Devs | [n/N] Next/Prev")

        try:
            user_input = input("> ").strip()
            if user_input.lower() == 'q': break
            elif not user_input: current_index += page_size
            elif user_input.lower() == 'b': current_index = max(0, current_index - page_size)
            elif user_input.lower() == 'c':
                if filter_mode is not None:
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
                else: print("Not in filter mode."); input()
            
            elif filter_mode is None:
                if user_input.lower() == 'l':
                    devices = get_unique_devices(entries)
                    show_device_list(devices)
                elif user_input.lower().startswith('i '):
                    args_str = user_input[2:].strip()
                    parts = args_str.split()
                    found = False
                    t_type, t_id = None, None
                    if len(parts) == 2:
                        t_type, t_id_str = parts
                        try: t_id = int(t_id_str); found = True
                        except ValueError: pass
                    elif len(parts) == 1:
                        match = re.match(r'^([A-Z]+)(\d+)$', parts[0])
                        if match: t_type = match.group(1); t_id = int(match.group(2)); found = True
                    
                    if found:
                        show_device_interactions(entries, mac_to_device_map, t_type, t_id)
                    else:
                        print("Usage: i <type><id> or i <type> <id> (e.g., i GW2 or i GW 2)"); input()

                elif user_input.lower().startswith('s '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        search_term = f"MAC: {parts[1]}"
                        term_lower = parts[1].lower()
                        search_indices = [i for i, e in enumerate(entries) if term_lower in e.mac.lower() or (e.src_mac and term_lower in e.src_mac.lower()) or (e.dst_mac and term_lower in e.dst_mac.lower())]
                        current_search_pos = -1
                        if search_indices: current_search_pos = 0; current_index = search_indices[0]
                        else: print(f"No matches found for MAC '{parts[1]}'. Press Enter..."); input() 
                    else: print("Usage: s <mac_address>"); input()
                elif user_input.lower().startswith('f '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_pid = int(parts[1]); filtered_target_id = target_pid
                            filtered_indices = [i for i, e in enumerate(entries) if e.packet_id == target_pid]
                            if filtered_indices: filter_mode = 'packet'; current_index = 0; search_indices = []; search_term = ""
                            else: print(f"No events found for Packet ID '{target_pid}'. Press Enter..."); input()
                        except ValueError: print("Invalid Packet ID. Use integer (e.g., 42)"); input()
                    else: print("Usage: f <packet_id>"); input()
                elif user_input.lower().startswith('p '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_pid = int(parts[1]); search_term = f"Packet ID: {target_pid}"
                            search_indices = [i for i, e in enumerate(entries) if e.packet_id == target_pid]
                            current_search_pos = -1
                            if search_indices: current_search_pos = 0; current_index = search_indices[0]
                            else: print(f"No matches found for Packet ID '{target_pid}'. Press Enter..."); input()
                        except ValueError: print("Invalid Packet ID. Use integer (e.g., 42)"); input()
                    else: print("Usage: p <packet_id>"); input()
                elif user_input.lower().startswith('d '):
                    args_str = user_input[2:].strip()
                    if not args_str: print("Usage: d <type><id> | <type><id> ... (e.g., d SAT0 | UT1)"); input()
                    else:
                        raw_parts = args_str.split('|'); target_devices = []; valid_input = True
                        for part in raw_parts:
                            clean_part = part.strip()
                            match = re.match(r'^([A-Z]+)(\d+)$', clean_part)
                            if match: target_devices.append((match.group(1), int(match.group(2))))
                            else: print(f"Invalid device format: '{clean_part}'. Expected 'TypeID' (e.g., SAT0)."); valid_input = False; break
                        if valid_input and target_devices:
                            desc_parts = [f"{t}{i}" for t, i in target_devices]; filter_description = ", ".join(desc_parts)
                            filtered_indices = [i for i, e in enumerate(entries) if (e.node_type, e.node_id) in target_devices]
                            if filtered_indices: filter_mode = 'device'; current_index = 0; search_indices = []; search_term = ""
                            else: print(f"No events found for devices: {filter_description}. Press Enter..."); input()
                        elif valid_input: print("No valid devices provided."); input()
                        else: input()
                elif user_input.lower() == 'n':
                    if search_indices:
                        if current_search_pos + 1 < len(search_indices): current_search_pos += 1; current_index = search_indices[current_search_pos]
                        else: print("End of search results."); input()
                    else: print("No active search. Use 's <mac>' or 'p <id>' first."); input()
                elif user_input == 'N':
                    if search_indices:
                        if current_search_pos > 0: current_search_pos -= 1; current_index = search_indices[current_search_pos]
                        else: print("Start of search results."); input()
                    else: print("No active search. Use 's <mac>' or 'p <id>' first."); input()
                elif user_input.lower().startswith('t '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try: target_time = float(parts[1]); idx = bisect.bisect_left(entry_times, target_time); current_index = idx if idx < total_entries else total_entries - 1
                        except ValueError: print("Invalid time format. Use float (e.g., 1.5)"); input()
                    else: print("Usage: t <time>"); input()
            else: print("Command not available in Filter Mode. Press [c] to exit filter first."); input()
        except (EOFError, KeyboardInterrupt): break

    print(f"\n{Colors.BOLD}End of trace.{Colors.RESET}")

def main():
    parser = argparse.ArgumentParser(description='Display Satellite Packet Trace')
    parser.add_argument('log_file', type=str, help='Path to PacketTrace.log file')
    parser.add_argument('--page-size', type=int, default=20, help='Number of lines per page')
    args = parser.parse_args()
    if not os.path.exists(args.log_file):
        print(f"Error: File '{args.log_file}' not found."); sys.exit(1)
    run_display(args.log_file, args.page_size)

if __name__ == '__main__':
    main()