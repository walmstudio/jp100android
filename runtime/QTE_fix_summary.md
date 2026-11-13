## QTE-иконки в Jurassic Park: The Game

**Цель:** восстановить интерактивные иконки (QTE, подсказки) в Android‑порте Jurassic Park: The Game. **Ограничение:** правки только через `.patch`.

**Контекст:** статическая рекомпиляция x86→C, GLES 3.0 бэкенд (`hle_d3d9_gl.h`, встроен в `hle_d3d9.c`).

**Файлы и ключевые факты**

**`/home/webmast/jp100-port/me1androd/runtime/hle_d3d9.c`**
- `g_cap_draws`, `g_cap_prims`, `g_cap_skipped`, `g_cap_last_skipped` — счётчики кадра.
- `m_SetTexture`, `m_SetTextureStageState` — функции для настройки текстур.
- QTE‑связанные функции: 45 совпадений по `grep`.
- Исследованы: `rs_fast`, `ss_fast`, `m_ValidateDevice`, `m_GetCurrentTexturePalette`, `m_SetScissorRect`, `m_GetScissorRect`, `m_GetNPatchMode`, `draw`.

**`/home/webmast/jp100-port/me1androd/runtime/hle_d3d9_gl.h`**
- `draws`, `skipped`, `texup`, `texbytes`, `bufup`, `bufbytes`, `ring`, `progs`, `progfail`, `progbin`, `fbos`, `clears`, `blits`, `shfail`, `shtrans`, `decoded`, **`rhw`**, `texmem` (signed), `tbuilt` — статистика.
- Ленивое создание GL‑объектов (`GlRes` в `Obj.gx`), `glTexStorage` для текстур, `glWaitSync` для синхронизации контекстов.
- Инициализация шейдера (строки 973–977): `if (g_gl != 1 || o->gx) return; ... o->gx = s; s->serial = ++g_serial;`
- `prog_get(GlCtx* C, Obj* vso, Obj* pso)` (строка 1052): берёт `vs = vso->gx`, `ps = pso->gx`, компилирует если `!vs->done`, ключ программы `prog_key(vso, pso)` по CRC, логирует ошибки и время сборки.
- **RHW‑путь (pre‑transformed vertices, используется для 2D/UI/QTE):**
  - `draw_rhw(const Dev* d)` — строка 1344, возвращает 1 если в декларации вершин есть RHW.
  - `rhw_vs()` — строка 1352, возвращает вершинный шейдер для RHW.
  - Строка 1367: `if (s && s->ok) s->s.rhw = 1;` — установка флага в шейдере.
  - Строка 1419: `float one = vs->rhw && in->usage == 10 && in->index == 0 ? 1.f : 0.f;` — обработка diffuse для RHW.
  - Строки 1522–1551: в `draw()`:
    ```c
    int rhw = draw_rhw(d);
    Obj* vso = rhw ? rhw_vs() : d->vs;
    ...
    if (rhw) { GS.rhw++; ... }
    ```
  - Строка 2198: логирование статистики включает `GS.rhw`.

**Команды и результаты**
- `grep` по `hle_d3d9_gl.h` для `rhw` — 10 совпадений, выявлен полный RHW‑путь отрисовки.
- `grep` по `hle_d3d9.c` для QTE — 45 совпадений.

**Ошибки и решения**
- Пока не фиксировались — идёт исследование кода.

**Осталось сделать**
1. Найти, где именно QTE‑иконки подаются на отрисовку (вероятно, через RHW‑путь и `m_SetTexture`/`m_SetTextureStageState`).
2. Понять, почему они не отображаются: лимиты `g_cap_draws`/`g_cap_prims`, пропуск в `draw_rhw`, текстуры не биндятся, шейдер не компилируется, или стейты текстурных стадий игнорируются.
3. Подготовить `.patch` с исправлением (разблокировка отрисовки, фикс биндинга текстур, корректная обработка `D3DTSS_*` стадий для RHW‑геометрии).

**Дополнительные факты**
- `Sm3Sem` — структура для описания входных и выходных регистров в шейдере.
- `Sm3` — структура для описания шейдера.
- `draw_rhw` — функция для отрисовки пред‑трансформированных вершин (RHW).
- `rhw_vs` — функция для создания вершинного шейдера для RHW.
- `draw` — функция для отрисовки.
- `g_cap_draws`, `g_cap_prims`, `g_cap_skipped`, `g_cap_last_skipped` — счётчики кадра.
- `m_SetTexture`, `m_SetTextureStageState` — функции для настройки текстур.
- `m_ValidateDevice`, `m_GetCurrentTexturePalette`, `m_SetScissorRect`, `m_GetScissorRect`, `m_GetNPatchMode` — функции для проверки и настройки устройства.
- `rs_fast`, `ss_fast` — функции для быстрого отрисовки.

**Решение**
- Найти, где именно QTE‑иконки подаются на отрисовку (вероятно, через RHW‑путь и `m_SetTexture`/`m_SetTextureStageState`).
- Понять, почему они не отображаются: лимиты `g_cap_draws`/`g_cap_prims`, пропуск в `draw_rhw`, текстуры не биндятся, шейдер не компилируется, или стейты текстурных стадий игнорируются.
- Подготовить `.patch` с исправлением (разблокировка отрисовки, фикс биндинга текстур, корректная обработка `D3DTSS_*` стадий для RHW‑геометрии).