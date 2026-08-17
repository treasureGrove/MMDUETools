# LookDev Evaluation Scorecard

> Per-experiment scoring template
> Scores 0-5 per item. Weighted total determines acceptance.

---

## Technical Scores (40% of total)

| Item | Score (0-5) | Notes |
|---|---|---|
| Lighting Correctness | | Light behavior matches contract |
| Exposure Stability | | No unexpected brightness changes |
| World/Preview Consistency | | Same lights → same result |
| Multi-Light Stability | | No jumping/flickering |
| SkyLight Robustness | | Works with and without SkyLight |
| Shadow Correctness | | MMD ShadowMap works as expected |

---

## Character Scores (45% of total)

| Item | Score (0-5) | Notes |
|---|---|---|
| Face Readability | | Face reads clearly in all light |
| Face Light Stability | | Main light doesn't jump |
| Skin Quality | | Not plastic, clean bands |
| Hair Quality | | Clean shadows, directional highlights |
| Eye Quality | | Iris visible, catch light stable |
| Cloth Differentiation | | Different materials look different |
| Silhouette | | Clear edge definition |
| Shadow Color | | Tinted, not black |
| Highlight Control | | Subtle, not explosive |

---

## Robustness Scores (15% of total)

| Item | Score (0-5) | Notes |
|---|---|---|
| Directional Only | | T01 |
| Point Light | | T04 |
| Spot Light | | T05 |
| Rect Light | | T06 |
| Sky Only | | T03 |
| Mixed Light | | T07 |
| Back Light | | T08 |
| HDR | | T09 |

---

## Weighted Total

```
Technical_Total  = mean(Technical items)  × 0.40
Character_Total  = mean(Character items)  × 0.45
Robustness_Total = mean(Robustness items) × 0.15

Final_Score = Technical_Total + Character_Total + Robustness_Total  (range 0-5)
```

---

## Accept/Reject Decision

| Condition | Decision |
|---|---|
| Final_Score improves AND Technical_Total ≥ 3.5 | **ACCEPT** |
| Final_Score improves BUT Technical_Total < 3.5 | **REJECT** — technical regression |
| Final_Score same or worse | **REJECT** — no improvement |
| Any auto-reject in RegressionMatrix | **REJECT** — no visual review needed |
| 2+ Technical items dropped by ≥ 2 points | **REJECT** — significant technical regression |

---

## Gate Rule (from task book §十六)

> 只要"更漂亮"的修改导致 2 个以上 Technical Environment 明显退化，
> 拒绝该修改。如果该效果确实有艺术价值，
> 把它设计成 Style Parameter 而不是破坏 Core Lighting。
