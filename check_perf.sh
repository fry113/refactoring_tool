#!/bin/bash

# пути к папкам и файлам
TEST_DATA_FLDR="${PWD}/tests/tests_data"
ORIGIN_FILE="$TEST_DATA_FLDR/perf_example.cpp"
BAK_FILE="$TEST_DATA_FLDR/perf_example_bak.cpp"
REPORT_FILE="PERF_report.txt"
TOOL="./build/refactor_tool"
TMP_EXE="./tmp_perf_built"

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

echo "=== Запуск профилирования производительности с perf ==="

# сохраняем резервную копию исходного файла
cp "$ORIGIN_FILE" "$BAK_FILE"
echo "Резервная копия файла сохранена в $BAK_FILE"

# функция для сборки и запуска программы с perf
run_with_perf() {
    local label=$1

    echo "----------------------------------------" >> "$REPORT_FILE"
    echo "$label" >> "$REPORT_FILE"
    echo "----------------------------------------" >> "$REPORT_FILE"

    # компилируем файл с оптимизацией -O0 и отладочными символами -g
    COMPILE_LOG=$(g++ -std=c++17 -g -O0 "$ORIGIN_FILE" -o "$TMP_EXE" 2>&1)

    # проверка, что бинарник собрался
    if [ $? -ne 0 ]; then
        echo "[Ошибка компиляции]" >> "$REPORT_FILE"
        echo "$COMPILE_LOG" >> "$REPORT_FILE"
        return 1
    fi

    ####### ЗАМЕР ВРЕМЕНИ ЧЕРЕЗ PERF STAT #######
    # Запускаем perf stat и пишем результат во временный файл
    perf stat -o /tmp/perf_stat.txt "$TMP_EXE" > /dev/null 2>&1

    if [ -f "/tmp/perf_stat.txt" ]; then
        echo "=== Замер времени выполнения ===" >> "$REPORT_FILE"
        # Вырезаем из отчета perf stat только строчки с миллисекундами и секундами
        grep -E "task-clock|seconds time elapsed" /tmp/perf_stat.txt >> "$REPORT_FILE"
        echo "=================================" >> "$REPORT_FILE"
        echo "" >> "$REPORT_FILE"
        rm -f /tmp/perf_stat.txt
    fi

    ####### ПРОФИЛИРОВАНИЕ ФУНКЦИЙ ЧЕРЕЗ PERF RECORD #######
    # пишем perf.data в локальную папку /tmp/ (иначе ошибка записи из контейнера)
    perf record -e cpu-clock -o /tmp/perf.data "$TMP_EXE" > /dev/null 2>&1

    # проверяем, создался ли perf.data
    if [ ! -f "/tmp/perf.data" ]; then
        echo "[Ошибка perf record] " >> "$REPORT_FILE"
    else
        echo "=== Профиль функций ===" >> "$REPORT_FILE"
        # оставляем только связанное с текущим кейсом
        perf report --stdio -i /tmp/perf.data -s sym --percent-limit 0.5 2>&1 | \
        grep -E "basic_string\(|~basic_string|new|delete|memcpy|clear_page|Overhead" | \
        sed -E 's/std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >/std::string/g' >> "$REPORT_FILE"

        # удаляем бинарный отчет
        rm -f /tmp/perf.data
    fi

    # удаляем временный исполняемый файл
    rm -f "$TMP_EXE"
}

# сборка и проверка до изменений
echo "До рефакторинга..."
run_with_perf "До рефакторинга"

# запуск утилиты
echo "Отчет о рефакторинге..."
echo "----------------------------------------" >> "$REPORT_FILE"
echo "Отчет о рефакторинге" >> "$REPORT_FILE"
echo "----------------------------------------" >> "$REPORT_FILE"

# выводим замечания утилиты прямо в отчет
stdbuf -o0 -e0 $TOOL "$ORIGIN_FILE" -- -std=c++17 >> "$REPORT_FILE" 2>&1

if [ $? -ne 0 ]; then
    echo "[Предупреждение] " >> "$REPORT_FILE"
fi

# сборка и проверка после изменений утилиты
echo "После рефакторинга..."
run_with_perf "После рефакторинга"

# возвращаем оригинальный файл из бэкапа на место
mv "$BAK_FILE" "$ORIGIN_FILE"

# на всякий случай удаляем временные файлы, если тест прервался
rm -f "$TMP_EXE"
rm -f /tmp/perf.data

echo "==================================================="
echo "Тестирование производительности завершено"
echo "Результаты записаны в файл: $REPORT_FILE"
