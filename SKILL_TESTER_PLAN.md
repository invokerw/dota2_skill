# Skill Tester 实施计划

## 总目标

新建一个独立可执行 `skill_tester`, 让用户在窗口里:

1. 从左侧栏选英雄 (扫 `data/heroes/*.yaml`).
2. 选定英雄后底部出现该英雄的所有技能 (按 `1-4` 或点按钮).
3. 选中技能后, 按其 behavior 释放:
   - `NoTarget` -- 立即施放
   - `UnitTarget` -- 鼠标点 dummy 释放
   - `PointTarget` -- 鼠标点空地释放
4. 切英雄 = 整个 `World` 重建. 1 caster (Radiant) + 3 dummy (Dire), 不同 magic_resist.
5. 后期支持调参面板 (改 dummy max_health / magic_resist / armor).

依赖: 已有 raylib + 已加入的 raygui 4.0. 不引入新库.

每阶段: 一个 commit, commit 前必跑 `cmake --build build -j && ctest --test-dir build --output-on-failure`, 已有 129 个测试不能回归.

---

## Stage S1: HeroCatalog 模块

**Goal**: 抽出"扫 yaml -> 英雄/技能元数据"为独立模块.

**Success Criteria**:

- `include/dota/tools/hero_catalog.hpp` + `src/tools/hero_catalog.cpp` 编入 `dota_core`.
- `tests/test_hero_catalog.cpp`: 扫 `data/heroes/`, 断言 6 个英雄 (lion / lina / pudge / sven / earthshaker / juggernaut) 都被发现, 且每个英雄至少有 3 个技能, 关键 behavior 字段非空.

**Status**: Complete

### S1 设计

```cpp
namespace dota::tools {

struct AbilityMeta {
    std::string   name;         // e.g. "lion_earth_spike"
    std::uint32_t behavior;     // 复用 BehaviorFlag 位掩码
    TargetTeam    target_team;
    double        cast_range;
    double        cast_point;
    bool          is_passive;
    bool          is_channelled;
};

struct HeroEntry {
    std::string  yaml_name;     // 文件名 stem, e.g. "lion"
    std::string  display_name;  // hero.name 字段, e.g. "npc_dota_hero_lion"
    double       base_health  = 0.0;
    double       base_mana    = 0.0;
    double       base_armor   = 0.0;
    double       magic_resist = 0.25;
    std::vector<AbilityMeta> abilities;   // 按 yaml 顺序
};

class HeroCatalog {
public:
    // 扫 directory 下所有 *.yaml, 收集 HeroEntry. 非 hero 文件跳过.
    std::size_t scan(const std::string& directory);

    const std::vector<HeroEntry>& heroes() const { return heroes_; }
private:
    std::vector<HeroEntry> heroes_;
};

} // namespace dota::tools
```

实现注意:

- 直接用 yaml-cpp; 不复用 `AbilityRegistry::load_file` (registry 已经吞了 def 但没暴露 per-hero ability 列表).
- behavior 字段同样的 csv 解析: 复用 `parse_behavior_flags`.
- 文件如果没有顶层 `hero:` 块就跳过 (例如未来加 _dummy.yaml).

### S1 测试

`tests/test_hero_catalog.cpp`:

- `HeroCatalog::scan(DOTA_DATA_DIR "/heroes")` 返回 6.
- 找 lion, 校验 abilities 包含 `lion_earth_spike` 和 `lion_finger_of_death`.
- earth_spike 的 behavior `& UnitTarget != 0`.
- finger_of_death 的 cast_range > 0.

---

## Stage S2: skill_tester 壳子 + 场景

**Goal**: 一个空的 raylib 窗口, 1 caster (Lion 默认) + 3 dummy 可见.

**Success Criteria**:

- `cmake --build build -j --target skill_tester` 成功.
- `./build/skill_tester` 打开窗口, 看到 4 个圆 (1 绿 3 红) + HP 条 + 名字.
- `R` 重置, `ESC` 退出, 暂无别的交互.

**Status**: Not Started

### S2 设计

新建 `examples/skill_tester.cpp` (~250 行第一版).

复用 `duel_visual.cpp` 已有的:

- `ViewCamera` 投影
- `team_color` / `draw_unit` / `draw_projectile` / `draw_floating_text` -- 抽到新 header `examples/visual_common.hpp` (header-only, inline). 把 `RenderUnit` / `RenderProjectile` / `FloatingText` 一并搬过去, 让 `duel_visual` 也包它. 这是一次必要重构 -- 不抽就两份代码漂移.

`Scene` 类:

```cpp
class Scene {
public:
    Scene(HeroCatalog& cat);
    void rebuild_with_hero(std::size_t hero_index);
    void update(double dt);
    Unit* caster() const;
    const std::vector<Unit*>& dummies() const;
    World& world();
    // 飘字回调订阅, 复用 LiveSource 思路
};
```

Dummy 配置:

- 3 个 Dire, max_health = 6000, magic_resist 分别 0.0 / 0.25 / 0.5, base_armor 分别 0 / 5 / 10.
- 名字 "Dummy MR0%" / "Dummy MR25%" / "Dummy MR50%" (一眼能看出区别).
- 位置: 屏幕右侧 +400/+500/+600, y 偏 -150/0/+150.

### S2 CMakeLists

```cmake
add_executable(skill_tester examples/skill_tester.cpp)
target_link_libraries(skill_tester PRIVATE dota_core raylib raygui)
target_compile_definitions(skill_tester PRIVATE
    DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\")
```

不写 GTest (UI 类), 通过手动启动验证.

---

## Stage S3: raygui 面板 (英雄列表 + 技能栏)

**Goal**: 左侧栏列英雄, 点击切英雄; 底部栏列当前 caster 的技能, 显示 cd / mana.

**Success Criteria**:

- 左侧 240 px 宽的 ListView 显示所有英雄, 点击 → `Scene::rebuild_with_hero`.
- 底部 100 px 高的 ability bar, 4 个槽 (英雄技能少则空置), 显示 ability 短名 + cd 倒计时 + mana cost.
- 鼠标 hover 时 raygui 默认高亮.

**Status**: Not Started

### S3 UI 布局

```text
+--------+----------------------------------+
| Heroes | (战场)                            |
| Lion   |                                  |
| Lina*  |                                  |
| Pudge  |                                  |
| Sven   |                                  |
| ...    |                                  |
+--------+----------------------------------+
| [1] earth_spike   [2] hex   [3] mana_drain  [4] finger_of_death |
+-----------------------------------------------------------------+
```

raygui API 使用:

- `GuiListView(rect, items_csv, &scroll, &active)` 给英雄列表 (返回选中 index).
- 技能槽用 4 个 `GuiButton(rect, label)`. 自定义 label `TextFormat("[%d] %s\nCD %.1fs  MP %d", ...)`.

### S3 实现

`HeroCatalog` 在启动时 scan 一次. UI 部分单独抽到 `Panels::draw(...)` 函数, 不直接动 Scene.

事件流:

- ListView 返回新 index ≠ 当前 → `scene.rebuild_with_hero(new)`.
- Ability button 点击 / 数字键按下 → `scene.select_ability(slot_index)` (这里只记录选择, 实际施法逻辑放 S4).

---

## Stage S4: 施法交互

**Goal**: 选中技能后, 按其 behavior 进入瞄准模式; 在游戏区点击释放; 显示预览.

**Success Criteria**:

- 选中 `lion_earth_spike` (UnitTarget) → 鼠标 hover dummy 时 dummy 高亮 + 距离指示, 左键释放; 太远时显示红色提示.
- 选中 `pudge_meat_hook` (PointTarget) → 鼠标移动时画从 caster 到指针的虚线 + 终点 width 圆; 左键释放.
- 选中 `juggernaut_blade_fury` (NoTarget) → 立刻按 `Space` / 数字键再按一次释放 (避免选中=立即施法的歧义).
- 没法力 / 在 CD 时, 屏幕底部 toast 显示 "Not enough mana" / "On cooldown 2.3s".
- `Esc` 取消瞄准.

**Status**: Not Started

### S4 状态机

```cpp
enum class AimMode { None, AwaitUnitTarget, AwaitPointTarget, AwaitConfirmNoTarget };

struct Selection {
    Ability* ability   = nullptr;
    AimMode  mode      = AimMode::None;
};
```

切换规则:

- 选中 ability → 根据 behavior 决定 mode.
- 进入 mode 后, 主循环根据 mode 监听不同输入.

### S4 预览

`draw_preview(ability, mouse_world, caster_pos)`:

- AwaitPointTarget: 用 ability_special 里的 `width` (如肉钩) 或 `radius` (如 LSA) 画对应几何; 取不到就画一个十字标记.
- AwaitUnitTarget: 当前 hover 的 dummy 上画黄圈; 距离 > cast_range 时圈红.

需要从 `AbilityMeta` 加一个 `cast_range` 上限检查 (Catalog 已有). width / radius 的取法要看 `ability_special` -- 第一版可以只展示直线 (用一个固定 width 100 的占位), 详细数值后期再调.

### S4 Toast

底部固定 high y 的一行字, 1.5s 淡出. 复用 `FloatingText` 结构, world_pos 改成屏幕坐标即可 (或者新建 `ScreenToast`).

---

## Stage S5: 调参面板 stub

**Goal**: 右侧 240 px 面板, 摆 sliders, 实时改 dummy 属性.

**Success Criteria**:

- 4 个 `GuiSliderBar`: dummy max_health (100..10000), magic_resist (-1.0..1.0), base_armor (-10..30), attack_damage (0..200).
- 滑动后立即写到所有 dummy 的 `UnitStats` (热更新, 不重建 World).
- 一个 `GuiButton "Reset Dummies"` 把所有 dummy hp 拉满 + 移除所有 modifier.

**Status**: Not Started

### S5 实现

`Unit::stats()` 返回 const ref, 没暴露 mutator. 选项:

1. 给 `Unit` 加一个 `set_stats(UnitStats)` -- 最干净, 但要小心 max_health 变化时 hp 也要跟随.
2. 直接重建 dummy (留 caster).

第一版选 #2 (重建 dummy), 简单可靠. #1 留 TODO.

---

## 执行顺序与提交节奏

S1 → S2 → S3 → S4 → S5, 每个阶段一个 commit:

1. `feat: HeroCatalog 模块 (Stage S1)`
2. `feat: skill_tester 壳子 + 场景 (Stage S2)`
3. `feat: skill_tester raygui 英雄/技能面板 (Stage S3)`
4. `feat: skill_tester 施法交互 (Stage S4)`
5. `feat: skill_tester 调参面板 (Stage S5)`

每次 commit 前必跑:

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

S1 添加新单元测试; S2-S5 是 UI 改动, 不写 GTest, 通过手动启动验证.
