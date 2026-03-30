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
# Adapted for generic PacketTrace parsing
#

import argparse
from collections import namedtuple

# Структура данных для одной записи в логе
TraceEntry = namedtuple('TraceEntry', [
    'time', 'event', 'node_type', 'node_id', 'mac', 
    'level', 'direction', 'packet_id', 'src_mac', 'dst_mac'
])

def parse_packet_trace_line(line):
    """
    Парсит одну строку из PacketTrace.log.
    Пример строки:
    0.1 SND GW 3 00:00:00:00:00:11 ND FWD 0 
    0.1 ENQ GW 3 00:00:00:00:00:11 LLC FWD 0 00:00:00:00:00:11 ff:ff:ff:ff:ff:ff
    """
    line = line.strip()
    
    # Пропускаем заголовки и пустые строки
    if not line or line.startswith("COLUMN") or line.startswith("---"):
        return None

    parts = line.split()
    
    # Минимальное количество полей: Time, Event, Type, ID, MAC, Level, Dir, PktID
    if len(parts) < 8:
        return None

    try:
        time = float(parts[0])
        event = parts[1]
        node_type = parts[2]
        node_id = int(parts[3])
        mac = parts[4]
        level = parts[5]
        direction = parts[6]
        packet_id = int(parts[7])
        
        src_mac = None
        dst_mac = None

        # Если есть дополнительные поля (Source MAC, Dest MAC)
        # В некоторых форматах логов может быть 9 полей (только Src), но здесь ожидаем 10 (Src Dst)
        if len(parts) >= 10:
            src_mac = parts[8]
            dst_mac = parts[9]
        elif len(parts) == 9:
            # Редкий случай, но обрабатываем: если только один MAC, считаем его Source
            src_mac = parts[8]

        return TraceEntry(time, event, node_type, node_id, mac, 
                          level, direction, packet_id, src_mac, dst_mac)
    except (ValueError, IndexError):
        # Если формат строки неожиданный, пропускаем её
        return None

def read_trace_file(filename):
    """
    Генератор, который читает файл и выдает объекты TraceEntry.
    """
    with open(filename, 'r') as f:
        for line in f:
            entry = parse_packet_trace_line(line)
            if entry:
                yield entry

def command_line_parser():
    parser = argparse.ArgumentParser(description='Generic Satellite Packet Trace Parser')
    parser.add_argument('log_file', type=str, help='Path to PacketTrace.log file')
    return parser

if __name__ == '__main__':
    # Тестовый вывод при прямом запуске
    args = command_line_parser().parse_args()
    for entry in read_trace_file(args.log_file):
        print(entry)