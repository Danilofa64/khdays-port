"""Check scope hook generation without regenerating the complete port."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare  # noqa: E402


class ScopeHooks(unittest.TestCase):
    def test_parameter_comment(self):
        source = "void Example(int *menu, int *entry /* reloaded after every call, safely */, int *node) { return; }"
        output = prepare.ui_hook(source, "synthetic.c", "Example", "text", "mission_select.objectives", "scope")
        self.assertIn("Example__khdays_game(menu, entry, node);", output)
        wrapper = output[output.rfind("void Example(") :]
        self.assertLess(wrapper.index("scope_push("), wrapper.index("Example__khdays_game("))
        self.assertLess(wrapper.index("Example__khdays_game("), wrapper.index("scope_pop("))

    def test_mission_hooks(self):
        count = 0
        for rel, hooks in prepare.UI_HOOKS.items():
            if not any(hook[3] == "scope" for hook in hooks):
                continue
            text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
            for hook in hooks:
                text = prepare.ui_hook(text, rel, *hook)
            if rel.endswith("MissionMenu_DrawInfo.c"):
                prefix = "Ov008" if "ov008" in rel else "Ov025"
                self.assertIn(f"{prefix}_MissionMenu_DrawInfo__khdays_game(pMenu, pEntry, pNode);", text)
            for before, after in prepare.FILE_HOOKS.get(rel, []):
                self.assertEqual(text.count(before), 1, (rel, before))
                text = text.replace(before, after)
            count += 1
        self.assertEqual(count, 12)

    def test_initial_instruction_and_world(self):
        count = 0
        for rel, hooks in prepare.FILE_HOOKS.items():
            if not rel.endswith("MainMenu_SetupTextSurfaces.c"):
                continue
            text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
            for before, after in hooks:
                self.assertEqual(text.count(before), 1, (rel, before))
                text = text.replace(before, after)
            self.assertEqual(text.count('scope_push("mission_select.instruction")'), 1)
            self.assertEqual(text.count('scope_push("mission_select.world")'), 1)
            self.assertEqual(text.count("khdays_ui_text_scope_pop();"), 2)
            count += 1
        self.assertEqual(count, 2)


if __name__ == "__main__":
    unittest.main()
