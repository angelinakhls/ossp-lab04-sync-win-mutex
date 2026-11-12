# -*- coding: utf-8 -*-
"""Генератор отчёта по лабораторной работе 4 (вариант 28).

Запуск из корня репозитория:
    python report/build_report.py
Результат: report/Lab4_Report.docx и report/Lab4_Report.pdf (PDF - при наличии LibreOffice).
Все числа и графики берутся из data/experiment_results.csv; если данных нет, на их месте
остаются выделенные жёлтым заглушки. Приложение А (ИИ) студент заполняет сам в файле
report/appendix_ai.txt - генератор вставляет его без изменений.
Зависимости: python-docx, matplotlib (для графиков), Pillow.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING, WD_COLOR_INDEX, WD_TAB_ALIGNMENT, WD_TAB_LEADER, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, HERE)
import analysis  # noqa: E402
import report_text as T  # noqa: E402

FIG_DIR = os.environ.get("LAB04_FIG", os.path.join(HERE, "figures"))
OUT_DOCX = os.environ.get("LAB04_OUT", os.path.join(HERE, "Lab4_Report.docx"))
FONT = "Times New Roman"
MONO = "Courier New"
TEXT_WIDTH_CM = 16.5  # 21,0 - 3,0 - 1,5

META = {"department": "", "group": "", "full_name": "", "teacher": "", "commit_hash": "",
        "os": "", "cpu": "", "compiler": "", "city_year": "Минск 2026"}
try:
    with open(os.path.join(HERE, "meta.json"), encoding="utf-8") as _f:
        META.update(json.load(_f))
except OSError:
    pass


def ru(x, nd=1):
    """Число с десятичной запятой."""
    return f"{x:.{nd}f}".replace(".", ",")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read().replace("\r\n", "\n")


def extract_func(path, signature):
    """Фрагмент от строки с signature до закрывающей '}' в первой колонке."""
    out, on = [], False
    for line in read(path).split("\n"):
        if not on and signature in line and not line.startswith(" " * 8):
            on = True
        if on:
            out.append(line)
            if line == "}":
                break
    return "\n".join(out)


def extract_block(path, start, end_line):
    out, on = [], False
    for line in read(path).split("\n"):
        if not on and start in line:
            on = True
        if on:
            out.append(line)
            if line == end_line:
                break
    return "\n".join(out)


class Builder:
    def __init__(self, page_map):
        self.page_map = page_map
        self.doc = Document()
        self.cites = {}
        self.toc = []          # (level, text)
        self.fig = 0
        self.tbl = 0
        self.lst = 0
        self._setup()

    # ---------------------------------------------------------------- setup
    def _font(self, style, size=14, bold=False):
        style.font.name = FONT
        style.font.size = Pt(size)
        style.font.bold = bold
        style.font.italic = False
        style.font.color.rgb = RGBColor(0, 0, 0)
        rpr = style.element.get_or_add_rPr()
        rf = rpr.find(qn("w:rFonts"))
        if rf is None:
            rf = OxmlElement("w:rFonts")
            rpr.append(rf)
        for a in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
            rf.set(qn(a), FONT)
        for a in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
            if rf.get(qn(a)) is not None:
                del rf.attrib[qn(a)]

    def _setup(self):
        d = self.doc
        s = d.sections[0]
        s.page_width, s.page_height = Cm(21.0), Cm(29.7)
        s.left_margin, s.right_margin = Cm(3.0), Cm(1.5)
        s.top_margin, s.bottom_margin = Cm(2.0), Cm(2.0)
        s.different_first_page_header_footer = True
        # номер страницы - справа внизу, на титульном листе не ставится
        fp = s.footer.paragraphs[0]
        fp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        self._field(fp, "PAGE")
        for r in fp.runs:
            r.font.name, r.font.size = FONT, Pt(14)

        st = d.styles
        n = st["Normal"]
        self._font(n)
        pf = n.paragraph_format
        pf.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
        pf.first_line_indent = Cm(1.25)
        pf.line_spacing_rule = WD_LINE_SPACING.EXACTLY
        pf.line_spacing = Pt(18)
        pf.space_before = pf.space_after = Pt(0)
        pf.widow_control = True
        for name, lvl in (("Heading 1", 1), ("Heading 2", 2)):
            h = st[name]
            self._font(h, 14, True)
            hf = h.paragraph_format
            hf.alignment = WD_ALIGN_PARAGRAPH.LEFT
            hf.first_line_indent = Cm(1.25)
            hf.line_spacing_rule = WD_LINE_SPACING.EXACTLY
            hf.line_spacing = Pt(18)
            hf.space_before = Pt(18 if lvl == 2 else 0)
            hf.space_after = Pt(18)       # пробельная строка после заголовка
            hf.keep_with_next = True
        for name, ind in (("TOC 1", 0), ("TOC 2", 0.5)):
            try:
                t = st[name]
            except KeyError:
                t = st.add_style(name, 1)
            self._font(t)
            tf = t.paragraph_format
            tf.alignment = WD_ALIGN_PARAGRAPH.LEFT
            tf.first_line_indent = Cm(0)
            tf.left_indent = Cm(ind)
            tf.line_spacing_rule = WD_LINE_SPACING.EXACTLY
            tf.line_spacing = Pt(18)
            tf.tab_stops.add_tab_stop(Cm(TEXT_WIDTH_CM), WD_TAB_ALIGNMENT.RIGHT, WD_TAB_LEADER.DOTS)

    # -------------------------------------------------------------- helpers
    @staticmethod
    def _field(par, instr):
        for typ, txt in (("begin", None), (None, instr), ("separate", None), (None, "1"), ("end", None)):
            r = par.add_run()
            if typ:
                fc = OxmlElement("w:fldChar")
                fc.set(qn("w:fldCharType"), typ)
                r._r.append(fc)
            elif txt == instr:
                it = OxmlElement("w:instrText")
                it.set(qn("xml:space"), "preserve")
                it.text = f" {instr} "
                r._r.append(it)
            else:
                r.text = txt

    def cite_num(self, key):
        if key not in self.cites:
            self.cites[key] = len(self.cites) + 1
        return self.cites[key]

    def runs(self, par, text, size=None, bold=None, mono=False):
        """Разметка: [@key] - ссылка на источник, {{...}} - заглушка (жёлтая), `код` - моноширинный."""
        text = re.sub(r"\[@(\w+)\]", lambda m: f"[{self.cite_num(m.group(1))}]", text)
        for tok in re.split(r"(\{\{.*?\}\}|`[^`]*`)", text):
            if not tok:
                continue
            if tok.startswith("{{"):
                r = par.add_run(tok[2:-2])
                r.font.highlight_color = WD_COLOR_INDEX.YELLOW
            elif tok.startswith("`"):
                r = par.add_run(tok[1:-1])
                r.font.name = MONO
                r._r.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), MONO)
                r.font.size = Pt((size or 14) - 2)
                continue
            else:
                r = par.add_run(tok)
            if size:
                r.font.size = Pt(size)
            if bold is not None:
                r.font.bold = bold
        return par

    def p(self, text, **kw):
        par = self.doc.add_paragraph()
        self.runs(par, text)
        for k, v in kw.items():
            setattr(par.paragraph_format, k, v)
        return par

    def blank(self):
        par = self.doc.add_paragraph()
        par.paragraph_format.first_line_indent = Cm(0)
        return par

    def bullets(self, items):
        """Простое перечисление: тире, точка с запятой в конце, последний элемент - точка."""
        for i, it in enumerate(items):
            it = it.rstrip(";.")
            self.p("– " + it + ("." if i == len(items) - 1 else ";"))

    def numbered(self, items):
        for i, it in enumerate(items, 1):
            self.p(f"{i} {it}")

    def heading(self, text, level=1, numbered_text=None, page_break=True, center=False, entry=None):
        par = self.doc.add_paragraph(style=f"Heading {level}")
        if center:
            par.alignment = WD_ALIGN_PARAGRAPH.CENTER
            par.paragraph_format.first_line_indent = Cm(0)
        if level == 1 and page_break:
            par.paragraph_format.page_break_before = True
        par.add_run(text)
        self.toc.append((level, entry or text))
        return par

    def appendix_heading(self, letter, status, title):
        par = self.doc.add_paragraph(style="Heading 1")
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        par.paragraph_format.first_line_indent = Cm(0)
        par.paragraph_format.page_break_before = True
        par.add_run(f"ПРИЛОЖЕНИЕ {letter}")
        par.add_run().add_break()
        r = par.add_run(f"({status})")
        r.font.bold = False
        r.add_break()
        par.add_run(title)
        self.toc.append((1, f"Приложение {letter} {title}"))
        return par

    def table_caption(self, text):
        self.tbl += 1
        par = self.doc.add_paragraph()
        par.paragraph_format.first_line_indent = Cm(0)
        par.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
        par.paragraph_format.keep_with_next = True
        self.runs(par, f"Таблица {self.tbl} – {text}")
        return self.tbl

    def table(self, header, rows, widths, size=12, align=None):
        t = self.doc.add_table(rows=1, cols=len(header))
        t.style = "Table Grid"
        t.alignment = WD_TABLE_ALIGNMENT.CENTER
        t.autofit = False
        align = align or ["l"] * len(header)

        def fill(cell, text, bold=False, a="l"):
            cell.text = ""
            par = cell.paragraphs[0]
            pf = par.paragraph_format
            pf.first_line_indent = Cm(0)
            pf.line_spacing_rule = WD_LINE_SPACING.SINGLE
            pf.alignment = {"l": WD_ALIGN_PARAGRAPH.LEFT, "c": WD_ALIGN_PARAGRAPH.CENTER}[a]
            self.runs(par, str(text), size=size, bold=True if bold else None)

        for i, h in enumerate(header):
            fill(t.rows[0].cells[i], h, True, "c")
        trpr = t.rows[0]._tr.get_or_add_trPr()
        th = OxmlElement("w:tblHeader")
        th.set(qn("w:val"), "true")
        trpr.append(th)
        for row in rows:
            cells = t.add_row().cells
            for i, v in enumerate(row):
                fill(cells[i], v, False, align[i])
        tblPr = t._tbl.tblPr
        lay = OxmlElement("w:tblLayout")
        lay.set(qn("w:type"), "fixed")
        tblPr.append(lay)
        tw = tblPr.find(qn("w:tblW"))
        if tw is None:
            tw = OxmlElement("w:tblW")
            tblPr.append(tw)
        tw.set(qn("w:type"), "dxa")
        tw.set(qn("w:w"), str(int(sum(widths) * 567)))
        for i, gc in enumerate(t._tbl.tblGrid.findall(qn("w:gridCol"))):
            gc.set(qn("w:w"), str(int(widths[i] * 567)))
        for row in t.rows:
            cs = OxmlElement("w:cantSplit")
            row._tr.get_or_add_trPr().append(cs)
            for i, c in enumerate(row.cells):
                c.width = Cm(widths[i])
        self.blank()
        return t

    def figure(self, path, caption, width_cm):
        self.fig += 1
        par = self.doc.add_paragraph()
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        pf = par.paragraph_format
        pf.first_line_indent = Cm(0)
        pf.line_spacing_rule = WD_LINE_SPACING.SINGLE
        pf.keep_with_next = True
        w, h = Image.open(path).size
        width = min(width_cm, 15.5)
        if width * h / w > 21.0:
            width = 21.0 * w / h
        par.add_run().add_picture(path, width=Cm(width))
        cap = self.doc.add_paragraph()
        cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
        cap.paragraph_format.first_line_indent = Cm(0)
        self.runs(cap, f"Рисунок {self.fig} – {caption}")
        self.blank()
        return self.fig

    def code(self, text, caption=None, size=9):
        if caption:
            self.lst += 1
            par = self.doc.add_paragraph()
            par.paragraph_format.first_line_indent = Cm(0)
            par.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
            par.paragraph_format.keep_with_next = True
            self.runs(par, f"Листинг {self.lst} – {caption}")
        for line in text.rstrip("\n").split("\n"):
            par = self.doc.add_paragraph()
            pf = par.paragraph_format
            pf.first_line_indent = Cm(0)
            pf.alignment = WD_ALIGN_PARAGRAPH.LEFT
            pf.line_spacing_rule = WD_LINE_SPACING.SINGLE
            r = par.add_run(line.replace("\t", "    ") if line else " ")
            r.font.name = MONO
            r._r.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), MONO)
            r.font.size = Pt(size)
        if caption:
            self.blank()

    def placeholder(self, text):
        par = self.doc.add_paragraph()
        r = par.add_run(text.strip("{}"))
        r.font.highlight_color = WD_COLOR_INDEX.YELLOW
        return par

    # ------------------------------------------------------------ the report
    def build(self):
        data = analysis.load()
        groups = analysis.by_config(data)
        self.title_page()
        toc_marker = self.doc.add_paragraph()
        self.toc_heading_done = False

        # ВВЕДЕНИЕ
        self.heading("ВВЕДЕНИЕ", 1, center=True)
        for t in T.INTRO:
            self.p(t)

        self.heading("1 ЦЕЛЬ РАБОТЫ")
        for t in T.GOAL:
            self.p(t)

        self.heading("2 ПОСТАНОВКА ИНДИВИДУАЛЬНОГО ЗАДАНИЯ")
        self.heading("2.1 Условие варианта 28", 2, page_break=False)
        self.p(T.TASK_INTRO)
        self.bullets(T.TASK_ITEMS)
        self.heading("2.2 Индивидуальные параметры", 2, page_break=False)
        for t in T.TASK_IND:
            self.p(t)

        self.heading("3 КРАТКИЕ ТЕОРЕТИЧЕСКИЕ СВЕДЕНИЯ")
        for title, paras in T.THEORY:
            self.heading(title, 2, page_break=False)
            for t in paras:
                self.p(t)
            if title.startswith("3.3"):
                self.bullets(T.THEORY_CODES)
        for t in T.THEORY_AFTER:
            self.p(t)

        self.heading("4 АРХИТЕКТУРА РАЗРАБОТАННОЙ ПРОГРАММЫ")
        for t in T.ARCH:
            self.p(t)
        self.figure(os.path.join(ROOT, "diagrams", "arch.png"), "Архитектура программы: процессы и общие ресурсы", 13)
        self.p("Состав модулей приведён в таблице 1.")
        self.table_caption(T.ARCH_MODULES_CAPTION)
        self.table(["Файл", "Назначение"], T.ARCH_MODULES, [4.3, 12.2])

        self.heading("5 ОПИСАНИЕ ПРОЦЕССОВ И ОБЩИХ РЕСУРСОВ")
        self.p(T.PROC_INTRO)
        self.table_caption(T.PROC_TABLE_CAPTION)
        self.table(["Номер", "Роль", "Назначение"], T.PROC_TABLE, [1.8, 3.2, 11.5])
        self.p(T.SHARED_INTRO)
        self.table_caption(T.SHARED_CAPTION)
        self.table(["Поле", "Назначение"], T.SHARED_TABLE, [4.3, 12.2])
        self.p(T.PARAM_INTRO)
        self.table_caption(T.PARAM_CAPTION)
        self.table(["Параметр", "Назначение", "По умолчанию"], T.PARAM_TABLE, [4.6, 9.2, 2.7])
        for t in T.LOG_TEXT:
            self.p(t)

        self.heading("6 ОПИСАНИЕ ПРИМЕНЕННЫХ МЕХАНИЗМОВ СИНХРОНИЗАЦИИ")
        for t in T.SYNC_INTRO:
            self.p(t)
        self.table_caption(T.SYNC_CAPTION)
        self.table(["Механизм", "Назначение в программе"], T.SYNC_TABLE, [5.3, 11.2])
        for title, paras in T.SYNC_SEC:
            self.heading(title, 2, page_break=False)
            for t in paras:
                self.p(t)
            if title.startswith("6.1"):
                self.code(extract_func("src/sync.cpp", "Acquire AcquireMutex("), "Ожидание Mutex с обработкой всех кодов возврата")
            if title.startswith("6.2"):
                self.code(extract_func("src/participant.cpp", "void HandleAbandoned("), "Обработка WAIT_ABANDONED и восстановление структуры")
                self.code(extract_func("src/participant.cpp", "int Attempt("), "Попытка доступа: ветвление по результату ожидания")
            if title.startswith("6.3"):
                self.code(extract_block("src/participant.cpp", "if (crashHere) {", "    }"), "Тестовое завершение процесса при удержании Mutex")

        self.heading("7 АЛГОРИТМ РАБОТЫ ПРОГРАММЫ")
        self.p("Порядок работы программы:")
        self.numbered(T.ALGO_STEPS)
        self.p("Блок-схема одной попытки доступа к структуре приведена на рисунке 2.")
        self.figure(os.path.join(ROOT, "diagrams", "activity_attempt.png"), "Алгоритм одной попытки доступа к общей структуре", 16)

        self.heading("8 СХЕМЫ И ДИАГРАММЫ ВЗАИМОДЕЙСТВИЯ")
        for t in T.DIAG_TEXT:
            self.p(t)
        self.figure(os.path.join(ROOT, "diagrams", "sequence_timeout.png"), "Взаимодействие при штатной работе и тайм-ауте", 14)
        self.figure(os.path.join(ROOT, "diagrams", "sequence_abandoned.png"), "Аварийное завершение Holder и обработка WAIT_ABANDONED", 14)
        self.table_caption(T.DIAG_CAPTION)
        self.table(["Рисунок", "Файл", "Путь в репозитории"], T.DIAG_TABLE, [2.8, 5.2, 8.5])
        self.p("Инструмент построения диаграмм – PlantUML; редактируемые исходные файлы указаны в таблице 6, их текст приведён в приложении Д.")

        self.heading("9 ПЕРЕЧЕНЬ ИСПОЛЬЗОВАННЫХ ФУНКЦИЙ WINDOWS API")
        self.p(T.API_INTRO)
        self.table_caption(T.API_CAPTION)
        self.table(["Функция", "Назначение", "Модуль"], T.API_TABLE, [5.5, 7.0, 4.0])

        self.heading("10 ОПИСАНИЕ ПРОВЕРЯЕМЫХ ИНВАРИАНТОВ")
        self.p(T.INV_INTRO)
        self.table_caption(T.INV_CAPTION)
        self.table(["№", "Инвариант", "Кто проверяет"], T.INV_TABLE, [1.2, 10.3, 5.0])
        for t in T.INV_AFTER:
            self.p(t)

        self.heading("11 МЕТОДИКА ПРОВЕДЕНИЯ ЭКСПЕРИМЕНТОВ")
        self.p("Условия проведения экспериментов приведены в таблице 9.")
        self.table_caption(T.METHOD_ENV_CAPTION)
        env = [("Операционная система", META["os"] or "{{указать: версия Windows}}"),
               ("Процессор", META["cpu"] or "{{указать: модель, число ядер}}"),
               ("Компилятор и режим сборки", META["compiler"] or "{{указать: например, MSVC 19.x, Release (/O2)}}"),
               ("Объекты", "3 процесса, один Mutex, одна область Shared Memory"),
               ("Таймер", "разрешение 1 мс (timeBeginPeriod(1))")]
        self.table(["Параметр", "Значение"], env, [5.5, 11.0])
        self.p("Исследуемые конфигурации указаны в таблице 10.")
        self.table_caption(T.METHOD_CONFIG_CAPTION)
        self.table(["Конфигурация", "Описание", "Имена в CSV"], T.METHOD_CONFIGS, [3.6, 8.4, 4.5])
        for t in T.METHOD_TEXT:
            self.p(t)

        self.results(groups)
        self.graphs(groups)

        # ЗАКЛЮЧЕНИЕ
        self.heading("ЗАКЛЮЧЕНИЕ", 1, center=True)
        self.p("В ходе выполнения лабораторной работы:")
        self.bullets(T.CONCL_ITEMS)
        self.conclusion_data(groups)

        # СПИСОК ИСТОЧНИКОВ -- заполняется после обхода текста (номера по порядку ссылок)
        self.heading("СПИСОК ИСПОЛЬЗОВАННЫХ ИСТОЧНИКОВ", 1, center=True)
        for key, num in sorted(self.cites.items(), key=lambda kv: kv[1]):
            par = self.doc.add_paragraph()
            par.alignment = WD_ALIGN_PARAGRAPH.LEFT
            self.runs(par, f"[{num}] {T.SOURCES[key]}")

        self.appendices(data)
        self.insert_toc(toc_marker)

    # ----------------------------------------------------------- title page
    def title_page(self):
        def line(text, bold=False, size=14, after=0, align=WD_ALIGN_PARAGRAPH.CENTER):
            par = self.doc.add_paragraph()
            par.alignment = align
            pf = par.paragraph_format
            pf.first_line_indent = Cm(0)
            pf.space_after = Pt(after)
            self.runs(par, text, size=size, bold=True if bold else None)
            return par
        line("МИНИСТЕРСТВО ОБРАЗОВАНИЯ РЕСПУБЛИКИ БЕЛАРУСЬ")
        line("Учреждение образования")
        line("«БЕЛОРУССКИЙ ГОСУДАРСТВЕННЫЙ УНИВЕРСИТЕТ", after=0)
        line("ИНФОРМАТИКИ И РАДИОЭЛЕКТРОНИКИ»", after=18)
        line("Кафедра " + (META["department"] or "{{название кафедры}}"), after=96)
        line("ОТЧЕТ", True, 16, 0)
        line("по лабораторной работе №4")
        line("«Взаимодействие процессов (потоков): взаимное исключение и синхронизация»", after=0)
        line("Вариант 28. Тайм-аут и аварийное освобождение Mutex", after=96)
        who = META["full_name"] or "Ангелина {{фамилия, инициалы}}"
        grp = META["group"] or "{{номер группы}}"
        line(f"Выполнила: студентка гр. {grp} {who} (STUDENT_ID = 114)", align=WD_ALIGN_PARAGRAPH.LEFT, after=12)
        line("Проверил: " + (META["teacher"] or "{{должность, фамилия, инициалы преподавателя}}"), align=WD_ALIGN_PARAGRAPH.LEFT, after=150)
        line(META["city_year"])

    # ------------------------------------------------------------------ TOC
    def insert_toc(self, marker):
        """Содержание: поле TOC Word с готовым (кэшированным) результатом."""
        paras = []
        head = self.doc.add_paragraph()
        head.alignment = WD_ALIGN_PARAGRAPH.CENTER
        head.paragraph_format.first_line_indent = Cm(0)
        head.paragraph_format.space_after = Pt(18)
        head.paragraph_format.page_break_before = True
        r = head.add_run("СОДЕРЖАНИЕ")
        r.font.bold = True
        paras.append(head)
        entries = self.toc
        for i, (lvl, text) in enumerate(entries):
            par = self.doc.add_paragraph(style=f"TOC {lvl}")
            if i == 0:
                self._fld(par, "begin")
                self._instr(par, ' TOC \\o "1-2" \\h \\z \\u ')
                self._fld(par, "separate")
            page = self.page_map.get(text, 0)
            par.add_run(text)
            par.add_run("\t" + (str(page) if page else ""))
            if i == len(entries) - 1:
                self._fld(par, "end")
            paras.append(par)
        for par in paras:
            marker._p.addprevious(par._p)
        marker._p.getparent().remove(marker._p)

    @staticmethod
    def _fld(par, typ):
        r = par.add_run()
        fc = OxmlElement("w:fldChar")
        fc.set(qn("w:fldCharType"), typ)
        r._r.append(fc)

    @staticmethod
    def _instr(par, text):
        r = par.add_run()
        it = OxmlElement("w:instrText")
        it.set(qn("xml:space"), "preserve")
        it.text = text
        r._r.append(it)

    # -------------------------------------------------------- results (12)
    def results(self, groups):
        self.heading("12 ЭКСПЕРИМЕНТАЛЬНЫЕ РЕЗУЛЬТАТЫ")
        have = bool(groups)
        if not have:
            self.placeholder("{{Данных нет: запустите lab04_sync.exe --suite experiments на Windows, затем python report/build_report.py. "
                             "Таблицы ниже будут заполнены значениями из data/experiment_results.csv.}}")
        if have:
            few = [n for n, r in groups.items() if len(r) < 3]
            if few:
                self.placeholder("{{Внимание: для конфигураций " + ", ".join(few) + " выполнено менее трёх запусков.}}")
        self.p("Результаты стандартного эксперимента (100 операций на рабочий процесс, SEED = 1) приведены в таблице 11, "
               "индивидуального эксперимента (670 операций, SEED = 12058) – в таблице 12. Время – от старта участников до завершения всех процессов.")
        labels = [("A", "Конфигурация А (тайм-аут 10 мс)"), ("B", "Конфигурация Б (тайм-аут 100 мс)"),
                  ("C", "Конфигурация В (тайм-аут 100 мс, тест аварии)")]
        for suffix, title in (("std", "Результаты эксперимента со стандартными параметрами"),
                              ("ind", "Результаты эксперимента с индивидуальными параметрами")):
            self.table_caption(title)
            rows = []
            for k, lab in labels:
                runs = groups.get(f"{k}_{suffix}", [])
                runs = sorted(runs, key=lambda r: r["run"])
                cells = [str(r["elapsed_ms"]) for r in runs[:3]] + ["–"] * (3 - len(runs[:3]))
                avg = ru(analysis.mean([r["elapsed_ms"] for r in runs])) if runs else "–"
                rows.append([lab] + cells + [avg])
            self.table(["Конфигурация", "Запуск 1, мс", "Запуск 2, мс", "Запуск 3, мс", "Среднее время, мс"], rows,
                       [5.7, 2.5, 2.5, 2.5, 3.3], align=["l", "c", "c", "c", "c"])

        self.p("Профильные показатели конфигураций (средние значения за все запуски) приведены в таблицах 13 и 14.")
        order = [("A_std", "А, стандартные"), ("B_std", "Б, стандартные"), ("C_std", "В, стандартные"),
                 ("A_ind", "А, индивидуальные"), ("B_ind", "Б, индивидуальные"), ("C_ind", "В, индивидуальные"),
                 ("Q_norecover", "Карантин (без восстановления)"), ("N_nosync", "Без Mutex (контроль)")]
        self.table_caption("Операции, тайм-ауты и восстановления (среднее за запуск)")
        rows1, rows2 = [], []
        for key, lab in order:
            if key in groups:
                a = analysis.aggregate(groups[key])
                rows1.append([lab, ru(a["w_success"], 0), ru(a["w_attempts"], 0), ru(a["w_timeouts"], 0),
                              ru(a["w_abandoned"], 0), ru(a["recoveries"], 0)])
                rows2.append([lab, ru(a["avg_wait_ms"], 2), ru(a["max_wait_ms"], 1), ru(a["holder_holds"], 0),
                              ru(a["blocked"], 0), ru(a["violations"], 0)])
            else:
                rows1.append([lab] + ["–"] * 5)
                rows2.append([lab] + ["–"] * 5)
        self.table(["Конфигурация", "Успешных операций Workers", "Попыток Workers", "WAIT_TIMEOUT", "WAIT_ABANDONED", "Восстан."],
                   rows1, [4.0, 2.7, 2.4, 2.6, 2.9, 1.9], size=10, align=["l"] + ["c"] * 5)
        self.table_caption("Время ожидания, удержания Holder, отказы и нарушения (среднее за запуск)")
        self.table(["Конфигурация", "Среднее ожидание, мс", "Макс. ожидание, мс", "Удержаний Holder", "Отказов", "Нарушений"],
                   rows2, [4.0, 2.6, 2.9, 2.4, 2.4, 2.2], size=10, align=["l"] + ["c"] * 5)

        sweep = sorted([(runs[0]["timeout_ms"], analysis.aggregate(runs)) for n, runs in groups.items() if n.startswith("S_t")],
                       key=lambda p: p[0])
        self.p("Результаты серии экспериментов по тайм-ауту (стандартные параметры) приведены в таблице 15.")
        self.table_caption("Зависимость показателей от тайм-аута (среднее за запуск)")
        rows = [[str(t), ru(a["elapsed_ms"]), ru(a["w_timeouts"], 0), ru(a["w_attempts"], 0), ru(a["avg_wait_ms"], 2), ru(a["max_wait_ms"], 1)]
                for t, a in sweep] or [["–"] * 6]
        self.table(["Тайм-аут, мс", "Время, мс", "WAIT_TIMEOUT", "Попыток Workers", "Среднее ожидание, мс", "Максимальное ожидание, мс"],
                   rows, [2.2, 2.2, 3.0, 2.8, 3.1, 3.2], size=10, align=["c"] * 6)

        self.p("Результаты проверок корректности (--suite tests) приведены в листинге ниже.")
        tr = os.path.join(ROOT, "data", "test_results.txt")
        if os.path.exists(tr):
            with open(tr, encoding="utf-8-sig") as f:
                self.code(f.read(), "Результаты прогона проверок корректности", 10)
        else:
            self.placeholder("{{Запустите lab04_sync.exe --suite tests: результат сохраняется в data/test_results.txt и будет вставлен сюда.}}")
        sl = os.path.join(ROOT, "data", "logs", "sample_crash.log")
        if os.path.exists(sl):
            with open(sl, encoding="utf-8", errors="replace") as f:
                keys = ("CRASH TEST", "WAIT_ABANDONED", "RECOVERED", "STARTED", "FINISHED", "EXPERIMENT START")
                sel = [ln.rstrip() for ln in f if any(k in ln for k in keys)][:20]
            if sel:
                self.p("Фрагмент журнала с аварийным завершением Holder и обработкой WAIT_ABANDONED приведён в листинге ниже.")
                self.code("\n".join(sel), "Фрагмент журнала при аварийном завершении Holder", 9)
        allok = groups and all(analysis.aggregate(groups[k])["all_ok"] for k in groups if k != "N_nosync")
        if groups:
            self.p("Во всех запусках с Mutex (кроме контрольных запусков без Mutex) итоговые проверки инвариантов "
                   + ("завершились без нарушений." if allok else "обнаружили нарушения; их причины указаны в журналах data/logs."))

    # ---------------------------------------------------------- graphs (13)
    def graphs(self, groups):
        self.heading("13 ГРАФИКИ И АНАЛИЗ РЕЗУЛЬТАТОВ")
        figs = [("fig1_time_vs_timeout.png", "Зависимость времени выполнения от тайм-аута (среднее, разброс min–max по трём запускам)"),
                ("fig2_timeouts_vs_timeout.png", "Зависимость числа WAIT_TIMEOUT и попыток Workers от тайм-аута"),
                ("fig3_wait_vs_timeout.png", "Зависимость времени ожидания Mutex от тайм-аута"),
                ("fig4_configs.png", "Среднее время выполнения конфигураций А, Б и В")]
        have = os.path.exists(os.path.join(FIG_DIR, figs[0][0]))
        n0 = self.fig
        self.p(f"Графики построены скриптом scripts/plot_results.py по реальным данным из data/experiment_results.csv (приложения В и Д). "
               f"Зависимость времени выполнения от тайм-аута показана на рисунке {n0 + 1}, числа тайм-аутов и попыток – на рисунке {n0 + 2}, "
               f"времени ожидания – на рисунке {n0 + 3}, сравнение конфигураций – на рисунке {n0 + 4}.")
        for fn, cap in figs:
            path = os.path.join(FIG_DIR, fn)
            if os.path.exists(path):
                self.figure(path, cap, 13)
            else:
                self.fig += 1
                self.placeholder(f"{{{{Рисунок {self.fig} – {cap}: запустите python scripts/plot_results.py после получения данных.}}}}")
        self.heading("13.1 Анализ результатов", 2, page_break=False)
        for t in self.analysis_text(groups):
            self.p(t)

    def analysis_text(self, groups):
        out = []
        out.append("Изменяемые параметры: тайм-аут ожидания Mutex у Workers (5–200 мс, основные значения 10 и 100 мс), режим работы (штатный и с тестовой аварией Holder), "
                   "число операций (100 и 670) и SEED (1 и 12058). Время удержания Mutex (50 мс) и пауза Holder (50 мс) в экспериментах не менялись.")
        out.append("Ожидаемый результат. Holder занимает Mutex на 50 мс из каждых примерно 100 мс. Если тайм-аут меньше времени удержания, Worker, "
                   "пришедший в начале удержания, получает WAIT_TIMEOUT несколько раз подряд; если тайм-аут превышает остаток удержания, ожидание заканчивается "
                   "освобождением Mutex и WAIT_TIMEOUT не возникает. Поэтому предполагалось, что число WAIT_TIMEOUT и число попыток убывают с ростом тайм-аута, "
                   "а время выполнения изменяется слабее, так как полезная работа Workers возможна только в паузах Holder.")
        if not groups:
            out.append("{{Полученные результаты: будут сформированы автоматически из data/experiment_results.csv после проведения экспериментов.}}")
            return out
        g = lambda k: analysis.aggregate(groups[k]) if k in groups else None
        sweep = sorted([(runs[0]["timeout_ms"], analysis.aggregate(runs)) for n, runs in groups.items() if n.startswith("S_t")], key=lambda p: p[0])
        res = []
        if len(sweep) >= 2:
            (t0, a0), (t1, a1) = sweep[0], sweep[-1]
            to_seq = [a["w_timeouts"] for _, a in sweep]
            mono = all(x >= y for x, y in zip(to_seq, to_seq[1:]))
            first_zero = next((t for t, a in sweep if a["w_timeouts"] == 0), None)
            el = [a["elapsed_ms"] for _, a in sweep]
            spread = (max(el) - min(el)) / min(el) * 100
            s = (f"Получено. При росте тайм-аута от {t0} до {t1} мс среднее число WAIT_TIMEOUT изменилось с {ru(a0['w_timeouts'], 0)} до {ru(a1['w_timeouts'], 0)}, "
                 f"число попыток Workers – с {ru(a0['w_attempts'], 0)} до {ru(a1['w_attempts'], 0)}, среднее время выполнения – с {ru(a0['elapsed_ms'])} до {ru(a1['elapsed_ms'])} мс. "
                 f"Число WAIT_TIMEOUT {'убывает монотонно' if mono else 'не является монотонно убывающим'}; ")
            s += (f"тайм-ауты отсутствуют начиная с {first_zero} мс. " if first_zero is not None else "нулевого числа тайм-аутов в серии не достигнуто. ")
            s += f"Среднее время выполнения в серии различается в пределах {ru(spread)} %."
            res.append(s)
        A, B, C = g("A_std"), g("B_std"), g("C_std")
        if A and B:
            d = A["elapsed_ms"] - B["elapsed_ms"]
            pct = abs(d) / B["elapsed_ms"] * 100
            word = ("практически не различается" if pct < 3 else ("больше" if d > 0 else "меньше"))
            res.append(f"Для стандартных параметров среднее время конфигурации А (10 мс) составило {ru(A['elapsed_ms'])} мс, конфигурации Б (100 мс) – {ru(B['elapsed_ms'])} мс; "
                       f"время при тайм-ауте 10 мс {word}" + ("" if pct < 3 else f" на {ru(abs(d))} мс ({ru(pct)} %)") + ". "
                       f"WAIT_TIMEOUT в среднем за запуск: {ru(A['w_timeouts'], 0)} при 10 мс и {ru(B['w_timeouts'], 0)} при 100 мс; "
                       f"попыток Workers: {ru(A['w_attempts'], 0)} и {ru(B['w_attempts'], 0)}. "
                       f"Максимальное ожидание Mutex одним Worker: {ru(A['max_wait_ms'])} мс при 10 мс и {ru(B['max_wait_ms'])} мс при 100 мс.")
        if C and B:
            d = C["elapsed_ms"] - B["elapsed_ms"]
            pct = abs(d) / B["elapsed_ms"] * 100
            word = ("практически не отличается от" if pct < 3 else ("больше, чем в" if d > 0 else "меньше, чем в"))
            tail = (" После аварии Holder больше не удерживает Mutex, поэтому оставшиеся операции Workers выполняются без ожидания его освобождения."
                    if d < 0 and pct >= 3 else "")
            res.append(f"В конфигурации В (тест аварии) WAIT_ABANDONED зафиксирован {ru(C['w_abandoned'], 0)} раз(а) за запуск, выполнено восстановлений: {ru(C['recoveries'], 0)}, "
                       f"нарушений инвариантов: {ru(C['violations'], 0)}. Среднее время {ru(C['elapsed_ms'])} мс {word} конфигурации Б ({ru(B['elapsed_ms'])} мс).{tail}")
        out.extend(res)
        ns = groups.get("N_nosync")
        sync_runs = [r for k, rs in groups.items() if k != "N_nosync" and k != "Q_norecover" for r in rs]
        if ns and sync_runs:
            lost = analysis.mean([r["expected_counter"] - r["final_counter"] for r in ns])
            viol = analysis.mean([r["violations"] for r in ns])
            out.append(f"Влияние синхронизации. В контрольном режиме без Mutex среднее число нарушений инвариантов за запуск составило {ru(viol, 0)}, "
                       f"потеряно в среднем {ru(lost, 0)} обновлений счётчика; во всех {len(sync_runs)} запусках с Mutex зарегистрировано нарушений: "
                       f"{sum(r['violations'] for r in sync_runs)}. Mutex сериализует операции над структурой, что исключает гонку, но делает "
                       "Workers зависимыми от времени удержания Holder: ожидание Mutex ограничено снизу остатком удержания, а при коротком тайм-ауте превращается в повторные попытки (опрос).")
        elif ns:
            out.append("Влияние синхронизации: в контрольном режиме без Mutex нарушения инвариантов обнаружены, с Mutex – нет.")
        Bi = g("B_ind")
        if B and Bi:
            r = Bi["elapsed_ms"] / B["elapsed_ms"]
            ratio_ops = Bi["ops"] / B["ops"]
            out.append(f"Нагрузка и производительность. При увеличении числа операций в {ru(ratio_ops)} раза (со {B['ops']} до {Bi['ops']}) среднее время конфигурации Б выросло в {ru(r)} раза "
                       f"({ru(B['elapsed_ms'])} и {ru(Bi['elapsed_ms'])} мс).")
        out.append("Изменение нагрузки не обязательно приводит к пропорциональному изменению производительности по следующим причинам. Во-первых, полезная работа Workers "
                   "возможна только пока Mutex свободен, поэтому пропускная способность ограничена долей времени, не занятой Holder, а не числом попыток. "
                   "Во-вторых, повторные попытки при коротком тайм-ауте увеличивают число обращений к ядру и переключений контекста, но не число выполненных операций. "
                   "В-третьих, в измеряемое время входят фиксированные затраты (создание процессов, ожидание события Start) и квантование задержек Sleep системным таймером. "
                   "В-четвёртых, два Workers конкурируют за один Mutex, и время ожидания зависит от порядка обслуживания, который документацией не гарантируется.")
        return out

    def conclusion_data(self, groups):
        if not groups:
            self.placeholder("{{Числовые выводы по результатам экспериментов будут добавлены автоматически после появления данных.}}")
            return
        B = analysis.aggregate(groups["B_std"]) if "B_std" in groups else None
        A = analysis.aggregate(groups["A_std"]) if "A_std" in groups else None
        if A and B:
            self.p(f"Показано, что при тайм-ауте 10 мс (меньше времени удержания 50 мс) в среднем за запуск происходит {ru(A['w_timeouts'], 0)} событий WAIT_TIMEOUT и {ru(A['w_attempts'], 0)} попыток Workers, "
                   f"а при тайм-ауте 100 мс – {ru(B['w_timeouts'], 0)} и {ru(B['w_attempts'], 0)} соответственно; среднее время выполнения составило {ru(A['elapsed_ms'])} и {ru(B['elapsed_ms'])} мс.")

    # ------------------------------------------------------------ appendices
    def appendices(self, data):
        # А - заполняется студентом самостоятельно
        self.appendix_heading("А", "обязательное", "Использование моделей искусственного интеллекта")
        path = os.path.join(HERE, "appendix_ai.txt")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f.read().split("\n"):
                    par = self.doc.add_paragraph()
                    par.paragraph_format.first_line_indent = Cm(0)
                    par.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
                    par.add_run(line)
        else:
            self.placeholder("{{Приложение заполняется студентом самостоятельно, без использования моделей ИИ. "
                             "Создайте файл report/appendix_ai.txt по шаблону из задания (инструмент, модель, цель, оригинальные промпты, результат, где использован, "
                             "самостоятельная проверка, доработки) либо укажите, что модели ИИ не использовались. Файл вставляется в отчёт без изменений.}}")
        # Б
        self.appendix_heading("Б", "обязательное", "Сведения о репозитории Git")
        self.p("Ссылка на репозиторий: https://github.com/angelinakhls/ossp-lab04-sync-win-mutex.git", first_line_indent=Cm(0))
        self.p("Хеш коммита окончательной версии: " + (META["commit_hash"] or "{{вставить: git rev-parse HEAD}}"), first_line_indent=Cm(0))
        self.p("Структура репозитория: src – исходный код; tests – проверки корректности; data – экспериментальные данные и журналы; scripts – запуск экспериментов и построение графиков; "
               "diagrams – исходные файлы диаграмм (PlantUML); report – генератор отчёта и отчёт; README.md – инструкции по сборке и воспроизведению.", first_line_indent=Cm(0))
        self.table_caption("Контрольные этапы разработки (хеши коммитов заполняются по git log)")
        self.table(["Этап", "Содержание", "Хеш коммита"],
                   [("1 Проектирование", "Структура проекта, архитектура, диаграммы", "{{хеш}}"),
                    ("2 Базовая реализация", "Процессы, состояния, начальная логика", "{{хеш}}"),
                    ("3 Синхронизация и тестирование", "Mutex, WAIT_TIMEOUT/WAIT_ABANDONED, инварианты, тесты", "{{хеш}}"),
                    ("4 Эксперименты и отчёт", "Данные, графики, отчёт", "{{хеш}}")], [4.6, 8.0, 3.9])
        # В
        self.appendix_heading("В", "обязательное", "Листинг кода генерации графических материалов")
        self.p("Файл scripts/plot_results.py (построение графиков) и scripts/analysis.py (обработка данных).", first_line_indent=Cm(0))
        self.code(read("scripts/plot_results.py"), "scripts/plot_results.py", 8)
        self.code(read("scripts/analysis.py"), "scripts/analysis.py", 8)
        # Г
        self.appendix_heading("Г", "обязательное", "Листинг кода исходного задания")
        for f in ["CMakeLists.txt", "src/common.h", "src/logger.h", "src/logger.cpp", "src/shared_memory.h", "src/shared_memory.cpp",
                  "src/sync.h", "src/sync.cpp", "src/participant.h", "src/participant.cpp", "src/parent.h", "src/parent.cpp", "src/main.cpp"]:
            self.code(read(f), f, 8)
        # Д
        self.appendix_heading("Д", "обязательное", "Исходные диаграммы, скрипты экспериментов и данные")
        self.p("Все диаграммы выполнены в PlantUML; файлы, инструмент построения и пути в репозитории указаны в таблице 6. Редактируемых файлов draw.io или Visio в работе нет.", first_line_indent=Cm(0))
        for f in ["arch.puml", "activity_attempt.puml", "sequence_timeout.puml", "sequence_abandoned.puml"]:
            self.code(read("diagrams/" + f), "diagrams/" + f, 8)
        for f in ["scripts/run_experiments.ps1", "tests/run_tests.ps1"]:
            self.code(read(f), f, 8)
        csvp = analysis.CSV_PATH
        if os.path.exists(csvp):
            with open(csvp, encoding="utf-8") as f:
                self.code(f.read().replace("\r\n", "\n"), "data/experiment_results.csv (экспериментальные данные)", 6)
        else:
            self.placeholder("{{data/experiment_results.csv: появится после выполнения scripts/run_experiments.ps1 и будет вставлен сюда.}}")

    def save(self, path):
        self.doc.save(path)


def convert_pdf(docx_path, outdir):
    cmd = os.environ.get("LAB04_SOFFICE_CMD")
    cmd = cmd.split() if cmd else [shutil.which("soffice") or shutil.which("soffice.exe") or ""]
    if not cmd[0]:
        return None
    try:
        subprocess.run(cmd + ["--headless", "--convert-to", "pdf", "--outdir", outdir, docx_path],
                       check=True, capture_output=True, timeout=300)
    except Exception as e:  # noqa: BLE001
        print("PDF conversion failed:", e)
        return None
    pdf = os.path.join(outdir, os.path.splitext(os.path.basename(docx_path))[0] + ".pdf")
    return pdf if os.path.exists(pdf) else None


def find_pages(pdf, entries):
    """Номера страниц заголовков по тексту PDF (для содержания)."""
    txt = subprocess.run(["pdftotext", "-layout", pdf, "-"], capture_output=True, text=True).stdout
    pages = txt.split("\f")
    norm = lambda s: re.sub(r"\s+", " ", s).strip()
    npages = [norm(p) for p in pages]
    start = 1
    while start < len(npages) and re.search(r"\.{4,}\s*\d*", pages[start]):
        start += 1
    result, cur = {}, start
    for lvl, text in entries:
        key = norm(text)
        if key.startswith("Приложение "):
            key = "ПРИЛОЖЕНИЕ " + key.split()[1] + " (ОБЯЗАТЕЛЬНОЕ)"
        for i in range(cur, len(npages)):
            if key in npages[i] or (lvl == 1 and key.upper() in npages[i].upper()):
                result[text] = i + 1
                cur = i
                break
    return result


def main():
    tmp = tempfile.mkdtemp()
    b = Builder({})
    b.build()
    first = os.path.join(tmp, "pass1.docx")
    b.save(first)
    pdf = convert_pdf(first, tmp)
    page_map = find_pages(pdf, b.toc) if pdf else {}
    if pdf:   # второй проход: номера страниц в содержании
        b = Builder(page_map)
        b.build()
    b.save(OUT_DOCX)
    final_pdf = convert_pdf(OUT_DOCX, os.path.dirname(OUT_DOCX))
    print("DOCX:", OUT_DOCX)
    print("PDF :", final_pdf or "не создан (нет LibreOffice: откройте DOCX в Word и сохраните как PDF)")
    missing = [t for _, t in b.toc if t not in page_map]
    if pdf and missing:
        print("Warning: page not found for:", missing[:5])


if __name__ == "__main__":
    main()
