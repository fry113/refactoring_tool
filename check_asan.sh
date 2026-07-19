#!/bin/bash

# пути к папкам и файлам
TEST_DATA_FLDR="${PWD}/tests/tests_data"
ORIGIN_FILE="$TEST_DATA_FLDR/leak_example.cpp"
BAK_FILE="$TEST_DATA_FLDR/leak_example_bak.cpp"
REPORT_FILE="ASAN_report.txt"
TOOL="./build/refactor_tool"
TMP_EXE="./tmp_leak_built"

# проверяем, существует ли утилита рефакторинга
if [ ! -f "$TOOL" ]; then
    echo "Ошибка: $TOOL не найдена"
    exit 1
fi

# проверяем, существует ли целевой файл для тестов
if [ ! -f "$ORIGIN_FILE" ]; then
    echo "Ошибка: $ORIGIN_FILE не найден"
    exit 1
fi

# создаем новый файл отчета
> "$REPORT_FILE"

echo "=== Запуск тестирования утечек памяти с ASAN ==="

# сохраняем резервную копию исходного файла
cp "$ORIGIN_FILE" "$BAK_FILE"
echo "Резервная копия файла сохранена в $BAK_FILE"

# функция для сборки и запуска программы с ASAN
run_with_ASAN() {
    local label=$1

    echo "----------------------------------------" >> "$REPORT_FILE"
    echo "$label" >> "$REPORT_FILE"
    echo "----------------------------------------" >> "$REPORT_FILE"

    # компилируем файл с включенным AddressSanitizer
    COMPILE_LOG=$(g++ -std=c++17 -fsanitize=address -static-libasan -g -O0 "$ORIGIN_FILE" -o "$TMP_EXE" 2>&1)

    # проверка, что бинарник собрался
    if [ $? -ne 0 ]; then
        echo "[Ошибка компиляции]" >> "$REPORT_FILE"
        echo "$COMPILE_LOG" >> "$REPORT_FILE"
        return 1
    fi

    # принудительно включаем детектор утечек памяти для ASAN
    export ASAN_OPTIONS=detect_leaks=1

    # запускаем через stdbuf и перехватываем вывод в переменную
    APP_OUTPUT=$(stdbuf -o0 -e0 "$TMP_EXE" 2>&1)

    # если приложение ничего не вывело, пишем статус успеха
    if [ -z "$APP_OUTPUT" ]; then
        echo "[Утечек не обнаружено]" >> "$REPORT_FILE"
    else
        echo "$APP_OUTPUT" >> "$REPORT_FILE"
    fi

    # удаляем временный исполняемый файл
    rm -f "$TMP_EXE"
}

# сборка и проверка до изменений
echo "До рефакторинга..."
run_with_ASAN "До рефакторинга"

# запуск утилиты
echo "Отчет об утечке (Запуск рефакторинга)..."
echo "----------------------------------------" >> "$REPORT_FILE"
echo "Отчет об утечке" >> "$REPORT_FILE"
echo "----------------------------------------" >> "$REPORT_FILE"

# выводим замечания утилиты прямо в отчет
stdbuf -o0 -e0 $TOOL "$ORIGIN_FILE" -- -std=c++17 >> "$REPORT_FILE" 2>&1

if [ $? -ne 0 ]; then
    echo "[Предупреждение] " >> "$REPORT_FILE"
fi

# сборка и проверка после изменений утилиты
echo "После рефакторинга..."
run_with_ASAN "После рефакторинга"

# возвращаем оригинальный файл из бэкапа на место
mv "$BAK_FILE" "$ORIGIN_FILE"

# на всякий случай удаляем временные файлы, если тест прервался
rm -f "$TMP_EXE"

echo "==================================================="
echo "Тестирование на наличие утечек завершено"
echo "Результаты записаны в файл: $REPORT_FILE"
