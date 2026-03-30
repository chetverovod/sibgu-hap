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

# Кроссплатформенные импорты для работы с клавиатурой
if os.name == 'nt': # Windows
    import msvcrt
else: # Linux/Mac
    import termios
    import tty

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

def get_key():
    """
    Считывает одну клавишу с клавиатуры.
    Поддерживает специальные клавиши (PageUp, PageDown, стрелки).
    Исправлены ошибки с кодами клавиш для Windows и Linux.
    """
    if os.name == 'nt':
        # --- Windows ---
        # msvcrt.kbhit() возвращает True, если есть нажатие в буфере
        if msvcrt.kbhit():
            ch = msvcrt.getch()
            # Специальные клавиши (функциональные, стрелки) возвращают 0x00 или 0xE0
            if ch in (b'\x00', b'\xe0'):
                ch2 = msvcrt.getch()
                # Коды скан-клавиш (Scan Codes)
                if ch2 == b'H': return 'UP'        # Стрелка вверх
                if ch2 == b'P': return 'DOWN'      # Стрелка вниз
                if ch2 == b'K': return 'LEFT'      # Стрелка влево
                if ch2 == b'M': return 'RIGHT'     # Стрелка вправо
                if ch2 == b'I': return 'PAGE_UP'   # Page Up (код 0x49 = 'I')
                if ch2 == b'Q': return 'PAGE_DOWN' # Page Down (код 0x51 = 'Q')
                if ch2 == b'S': return 'DEL'       # Delete
                return ch2.decode('utf-8', errors='ignore')
            elif ch == b'\r':
                return 'ENTER'
            elif ch == b'\x03': # Ctrl+C
                raise KeyboardInterrupt
            else:
                return ch.decode('utf-8', errors='ignore')
        return None
    else:
        # --- Linux / Mac ---
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(sys.stdin.fileno())
            ch = sys.stdin.read(1)
            
            # Escape последовательности начинаются с \x1b (ESC)
            if ch == '\x1b':
                # Читаем следующие 2 символа
                seq = sys.stdin.read(2)
                
                # Стрелки: [A, [B, [C, [D
                if seq == '[A': return 'UP'
                if seq == '[B': return 'DOWN'
                if seq == '[D': return 'LEFT'
                if seq == '[C': return 'RIGHT'
                
                # PageUp / PageDown: [5~, [6~
                # После '[5' идет еще один символ '~'
                if seq == '[5': 
                    # Читаем последний символ последовательности
                    if sys.stdin.read(1) == '~': return 'PAGE_UP'
                
                if seq == '[6': 
                    if sys.stdin.read(1) == '~': return 'PAGE_DOWN'
                
                # Если это что-то другое, просто возвращаем ESC
                return 'ESC'
            
            elif ch == '\r' or ch == '\n':
                return 'ENTER'
            elif ch == '\x03': # Ctrl+C
                raise KeyboardInterrupt
            else:
                return ch
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

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
    """Строит словарь MAC адрес -> (Type, ID)."""
    mac_map = {}
    for e in entries:
        if e.mac:
            mac_map[e.mac] = (e.node_type, e.node_id)
        if e.src_mac:
            mac_map[e.src_mac] = (e.node_type, e.node_id)
    return mac_map

def show_device_interactions(entries, mac_map, target_type, target_id):
    """Показывает список устройств, с которыми взаимодействовал указанный узел."""
    peers = set()
    broadcast_found = False
    unknown_found = False
    target_device = (target_type, target_id)

    for e in entries:
        src_dev = mac_map.get(e.src_mac)
        dst_dev = mac_map.get(e.dst_mac)

        if e.dst_mac:
            if e.dst_mac.lower() == "ff:ff:ff:ff:ff:ff":
                broadcast_found = True
            elif dst_dev is None:
                unknown_found = True

        if src_dev == target_device and dst_dev:
            peers.add(dst_dev)
        if dst_dev == target_device and src_dev:
            peers.add(src_dev)

    peers.discard(target_device)

    clear_screen()
    print(f"{Colors.BOLD}Interactions for Device: {target_type}{target_id}{Colors.RESET}")
    print("-" * 40)
    if not peers:
        print("No direct interactions found with specific devices.")
    sorted_peers = sorted(list(peers), key=lambda x: (x[0], x[1]))
    for p_type, p_id in sorted_peers:
        print(f"  {p_type:<5} | {p_id:<5}")

    footer_lines = []
    if broadcast_found:
        footer_lines.append(f"{Colors.CYAN}* Communicated via BROADCAST{Colors.RESET}")
    if unknown_found:
        footer_lines.append(f"{Colors.RED}* Communicated with UNKNOWN devices{Colors.RESET}")
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
            elif filter_mode == 'pair':
                status_suffix = f" | {Colors.BOLD}{Colors.GREEN}FILTERED PAIR: {filter_description}{Colors.RESET} ({total_viewable} events)"
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
        
        print(f"{Colors.BOLD}Controls:{Colors.RESET} [PgUp/PgDn] Scroll | [Enter] Next | [b] Back | [t] Jump | [l] List | [i] Interact |")
        print(f"{Colors.BOLD}         {Colors.RESET} [x] Filter Pair | [Q] Quit")        
        if filter_mode is not None:
            print(f"{Colors.BOLD}         {Colors.RESET} [c] Clear Filter")
        else:
            print(f"{Colors.BOLD}         {Colors.RESET} [s] Search MAC  | [p] Search Pkt  | [f] Filter Pkt      | [d] Filter Devs |")
            print(f"{Colors.BOLD}         {Colors.RESET} [n/N] Next/Prev Search")
        try:
            # Гибридный ввод
            key = get_key()
            
            # Обработка навигационных клавиш (одиночные действия)
            if key == 'PAGE_DOWN' or key == 'ENTER':
                current_index += page_size
                continue
            elif key == 'PAGE_UP':
                current_index = max(0, current_index - page_size)
                continue
            elif key == 'q' or key == 'Q':
                break
            elif key == 'b' or key == 'B':
                current_index = max(0, current_index - page_size)
                continue
            elif key == 'c' or key == 'C':
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
                continue
            elif key == 'l' or key == 'L':
                devices = get_unique_devices(entries)
                show_device_list(devices)
                continue

            # Если клавиша требует аргументов, используем обычный input
            # Но сначала выведем нажатую клавишу, так как get_key ее "съел"
            if key is not None and len(key) == 1:
                print(f"> {key}", end='', flush=True)
                rest = input("")
                user_input = (key + rest).strip()
            else:
                # На всякий случай, если что-то пошло не так (или нажат неопознанный спец. символ)
                user_input = input("> ").strip()

            # --- Обработка команд с аргументами ---
            if filter_mode is None:
                # Фильтрация пары
                if user_input.lower().startswith('x '):
                    args_str = user_input[2:].strip()
                    if '-' not in args_str:
                        print("Usage: x <Type1><ID1> - <Type2><ID2> (e.g., x SAT0 - UT2)")
                        input()
                    else:
                        parts = args_str.split('-', 1)
                        if len(parts) != 2:
                            print("Invalid format. Use dash '-' to separate devices.")
                            input()
                        else:
                            left_str = parts[0].strip()
                            right_str = parts[1].strip()
                            dev1 = None; dev2 = None
                            
                            l_parts = left_str.split()
                            if len(l_parts) == 1:
                                m = re.match(r'^([A-Z]+)(\d+)$', l_parts[0])
                                if m: dev1 = (m.group(1), int(m.group(2)))
                            elif len(l_parts) == 2:
                                try: dev1 = (l_parts[0], int(l_parts[1]))
                                except ValueError: pass
                            
                            r_parts = right_str.split()
                            if len(r_parts) == 1:
                                m = re.match(r'^([A-Z]+)(\d+)$', r_parts[0])
                                if m: dev2 = (m.group(1), int(m.group(2)))
                            elif len(r_parts) == 2:
                                try: dev2 = (r_parts[0], int(r_parts[1]))
                                except ValueError: pass
                                
                            if dev1 and dev2:
                                filter_description = f"{dev1[0]}{dev1[1]} - {dev2[0]}{dev2[1]}"
                                target_pair = {dev1, dev2}
                                filtered_indices = []
                                for i, e in enumerate(entries):
                                    src_dev = mac_to_device_map.get(e.src_mac)
                                    dst_dev = mac_to_device_map.get(e.dst_mac)
                                    if src_dev and dst_dev and {src_dev, dst_dev} == target_pair:
                                        filtered_indices.append(i)
                                if filtered_indices:
                                    filter_mode = 'pair'; current_index = 0; search_indices = []; search_term = ""
                                else: print(f"No interaction found. Press Enter..."); input()
                            else: print("Invalid device format. Use TypeID (e.g., SAT0)."); input()

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
                    if found: show_device_interactions(entries, mac_to_device_map, t_type, t_id)
                    else: print("Usage: i <type><id> or i <type> <id>"); input()

                elif user_input.lower().startswith('s '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        search_term = f"MAC: {parts[1]}"
                        term_lower = parts[1].lower()
                        search_indices = [i for i, e in enumerate(entries) if term_lower in e.mac.lower() or (e.src_mac and term_lower in e.src_mac.lower()) or (e.dst_mac and term_lower in e.dst_mac.lower())]
                        current_search_pos = -1
                        if search_indices: current_search_pos = 0; current_index = search_indices[0]
                        else: print(f"No matches found. Press Enter..."); input() 
                    else: print("Usage: s <mac_address>"); input()
                elif user_input.lower().startswith('f '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_pid = int(parts[1]); filtered_target_id = target_pid
                            filtered_indices = [i for i, e in enumerate(entries) if e.packet_id == target_pid]
                            if filtered_indices: filter_mode = 'packet'; current_index = 0; search_indices = []; search_term = ""
                            else: print(f"No events found. Press Enter..."); input()
                        except ValueError: print("Invalid Packet ID."); input()
                    else: print("Usage: f <packet_id>"); input()
                elif user_input.lower().startswith('p '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try:
                            target_pid = int(parts[1]); search_term = f"Packet ID: {target_pid}"
                            search_indices = [i for i, e in enumerate(entries) if e.packet_id == target_pid]
                            current_search_pos = -1
                            if search_indices: current_search_pos = 0; current_index = search_indices[0]
                            else: print(f"No matches found. Press Enter..."); input()
                        except ValueError: print("Invalid Packet ID."); input()
                    else: print("Usage: p <packet_id>"); input()
                elif user_input.lower().startswith('d '):
                    args_str = user_input[2:].strip()
                    if not args_str: print("Usage: d <type><id> | <type><id>..."); input()
                    else:
                        raw_parts = args_str.split('|'); target_devices = []; valid_input = True
                        for part in raw_parts:
                            clean_part = part.strip()
                            match = re.match(r'^([A-Z]+)(\d+)$', clean_part)
                            if match: target_devices.append((match.group(1), int(match.group(2))))
                            else: print(f"Invalid format: '{clean_part}'."); valid_input = False; break
                        if valid_input and target_devices:
                            desc_parts = [f"{t}{i}" for t, i in target_devices]; filter_description = ", ".join(desc_parts)
                            filtered_indices = [i for i, e in enumerate(entries) if (e.node_type, e.node_id) in target_devices]
                            if filtered_indices: filter_mode = 'device'; current_index = 0; search_indices = []; search_term = ""
                            else: print(f"No events found. Press Enter..."); input()
                        elif valid_input: print("No valid devices."); input()
                        else: input()
                elif user_input.lower() == 'n':
                    if search_indices:
                        if current_search_pos + 1 < len(search_indices): current_search_pos += 1; current_index = search_indices[current_search_pos]
                        else: print("End of search results."); input()
                    else: print("No active search."); input()
                elif user_input == 'N':
                    if search_indices:
                        if current_search_pos > 0: current_search_pos -= 1; current_index = search_indices[current_search_pos]
                        else: print("Start of search results."); input()
                    else: print("No active search."); input()
                elif user_input.lower().startswith('t '):
                    parts = user_input.split(maxsplit=1)
                    if len(parts) > 1:
                        try: target_time = float(parts[1]); idx = bisect.bisect_left(entry_times, target_time); current_index = idx if idx < total_entries else total_entries - 1
                        except ValueError: print("Invalid time format."); input()
                    else: print("Usage: t <time>"); input()
            else:
                print("Command not available in Filter Mode. Press [c] to exit filter first."); input()

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