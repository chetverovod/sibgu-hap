#!/usr/bin/env python3

import os
import glob
from pathlib import Path

def create_code_markdown(output_file="combined_code.md", input_files=None):
    """
    Создает markdown файл с кодом указанных файлов или всех .h/.cpp/.ino в текущей директории.
    
    Args:
        output_file (str): Имя выходного markdown файла
        input_files (list, optional): Список имен файлов для обработки. 
                                       Если None, то ищутся файлы в текущей директории.
    """
    
    current_dir = Path(".")
    all_files = []

    # Логика выбора файлов
    if input_files is None:
        # Режим по умолчанию: ищем все файлы в текущей директории
        cc_files = list(current_dir.glob("*.cc"))
        cpp_files = cc_files + list(current_dir.glob("*.cpp"))
        header_files = list(current_dir.glob("*.h"))
        ino_files = list(current_dir.glob("*.ino"))
        all_files = cpp_files + header_files + ino_files
    else:
        # Режим пользователя: обрабатываем только переданные файлы
        for f_name in input_files:
            path = Path(f_name)
            # Проверяем, существует ли файл, чтобы избежать ошибок позже
            if path.exists():
                all_files.append(path)
            else:
                print(f"Предупреждение: Файл '{f_name}' не найден и будет пропущен.")

    if not all_files:
        if input_files is None:
            print("Файлы .h, .cpp и .ino не найдены в текущей директории!")
        else:
            print("Не удалось найти ни один из указанных файлов.")
        return
    
    print(f"Найдено файлов для обработки: {len(all_files)}")
    
    with open(output_file, 'w', encoding='utf-8') as md_file:
        md_file.write("# Объединенный код проекта\n\n")
        md_file.write(f"*Директория: {os.path.abspath('.')}*\n\n")
        
        for file_path in sorted(all_files):
            print(f"Обрабатывается: {file_path.name}")
            
            # Заголовок для файла
            md_file.write(f"## Файл: `{file_path.name}`\n\n")
            
            # Определяем язык для подсветки синтаксиса
            suffix = file_path.suffix
            if suffix in [".cpp", ".cc", ".ino"]:
                lang = "cpp"
            else:
                lang = "c"
                
            md_file.write(f"```{lang}\n")
            
            try:
                # Читаем содержимое файла
                with open(file_path, 'r', encoding='utf-8') as source_file:
                    content = source_file.read()
                    md_file.write(content)
                    
                    # Добавляем перенос строки если файл не заканчивается им
                    if content and not content.endswith('\n'):
                        md_file.write('\n')
                        
            except UnicodeDecodeError:
                # Пробуем другие кодировки
                try:
                    with open(file_path, 'r', encoding='cp1251') as source_file:
                        content = source_file.read()
                        md_file.write(content)
                        if content and not content.endswith('\n'):
                            md_file.write('\n')
                except Exception as e:
                    md_file.write(f"// Ошибка чтения файла: {e}\n")
            except Exception as e:
                md_file.write(f"// Ошибка чтения файла: {e}\n")
            
            md_file.write("```\n\n")
    
    print(f"Готово! Результат сохранен в: {output_file}")
    
    # Выводим список обработанных файлов
    print("\nОбработанные файлы:")
    for file in sorted(all_files):
        print(f"  - {file.name}")

def main():
    """
    Основная функция программы
    """
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Объединяет файлы кода в один markdown файл.'
    )
    
    # Добавляем аргумент для списка файлов (nargs='*' означает 0 или более имен)
    parser.add_argument('files', nargs='*', default=None,
                       help='Список файлов для объединения (например main.cpp config.h). '
                            'Если не указаны, обрабатываются все .h/.cpp/.ino из текущей папки.')
    
    parser.add_argument('-o', '--output', default='combined_code.md',
                       help='Имя выходного markdown файла (по умолчанию: combined_code.md)')
    
    args = parser.parse_args()
    
    # Передаем список файлов в функцию
    create_code_markdown(args.output, args.files)

def simple_version():
    """
    Упрощенная версия без аргументов командной строки
    """
    output_file = "combined_code.md"
    current_dir = os.listdir(".")
    
    # Фильтруем только .h и .cpp файлы
    code_files = [f for f in current_dir 
                 if f.endswith(('.h', '.cpp')) and os.path.isfile(f)]
    
    if not code_files:
        print("Файлы .h и .cpp не найдены в текущей директории!")
        return
    
    print(f"Найдено файлов: {len(code_files)}")
    
    with open(output_file, 'w', encoding='utf-8') as md_file:
        md_file.write("# Объединенный код проекта\\n\\n")
        
        for filename in sorted(code_files):
            print(f"Обрабатывается: {filename}")
            md_file.write(f"## Файл: `{filename}`\\n\\n")
            
            lang = "cpp" if filename.endswith('.cpp') else "c"
            md_file.write(f"```{lang}\\n")
            
            try:
                with open(filename, 'r', encoding='utf-8') as f:
                    content = f.read()
                    md_file.write(content)
                    if content and not content.endswith('\\n'):
                        md_file.write('\\n')
            except Exception as e:
                md_file.write(f"// Ошибка чтения файла: {e}\\n")
            
            md_file.write("```\\n\\n")
    
    print(f"Готово! Файл сохранен как: {output_file}")

if __name__ == "__main__":
    main()
