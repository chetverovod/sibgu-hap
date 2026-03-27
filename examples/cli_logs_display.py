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

def format_entry(entry):
    """Форматирует одну запись лога в строку таблицы."""
    time_str = f"{entry.time:.6f}"
    
    event_color = get_color_for_event(entry.event)
    dir_color = get_color_for_direction(entry.direction)
    
    # Формирование информации о MAC адресах (если есть)
    mac_info = ""
    if entry.src_mac and entry.dst_mac:
        mac_info = f"{entry.src_mac} -> {entry.dst_mac}"
    elif entry.src_mac:
        mac_info = f"Src: {entry.src_mac}"
    
    # Форматирование строки с фиксированной шириной колонок для читаемости
    # Колонки: Time | Event | Node | ID | MAC | Lvl | Dir | PktID | MAC Info
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

    print(f"Loaded {total_entries} entries.")
    print("Controls: [Enter] - Next Page, [Q] - Quit\n")
    input("Press Enter to start...")

    current_index = 0

    while current_index < total_entries:
        clear_screen()
        print(f"{Colors.BOLD}Packet Trace Viewer | Page {current_index // page_size + 1}{Colors.RESET}")
        print_header()
        
        # Вывод страницы
        end_index = min(current_index + page_size, total_entries)
        for i in range(current_index, end_index):
            print(format_entry(entries[i]))
        
        print(f"\nShowing entries {current_index + 1} - {end_index} of {total_entries}")
        print("[Enter] Next | [Q] Quit")

        # Ожидание ввода пользователя
        try:
            user_input = input()
            if user_input.lower() == 'q':
                break
            current_index += page_size
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
