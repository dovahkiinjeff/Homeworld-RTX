import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image


MODULE_PATH = Path(__file__).parents[1] / "tools" / "ship_texture_pipeline.py"
SPEC = importlib.util.spec_from_file_location("ship_texture_pipeline", MODULE_PATH)
PIPELINE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
sys.modules[SPEC.name] = PIPELINE
SPEC.loader.exec_module(PIPELINE)


class ShipTexturePipelineTests(unittest.TestCase):
    def test_only_real_ship_trees_are_admitted(self):
        self.assertTrue(PIPELINE.is_ship_path(Path("R1/Mothership/rl0/lod0/hull.png")))
        self.assertTrue(PIPELINE.is_ship_path(Path("R2/Carrier/lod1/hull.png")))
        self.assertFalse(PIPELINE.is_ship_path(Path("UI/Dossier/R1/Carrier.png")))
        self.assertFalse(PIPELINE.is_ship_path(Path("Planets/kharak.png")))

    def test_categories_keep_technical_maps_out_of_ai(self):
        rgb = Image.new("RGB", (2, 2), "gray")
        self.assertEqual("albedo", PIPELINE.category_for(Path("hull.png"), rgb))
        self.assertEqual("mask", PIPELINE.category_for(Path("hull_teamEffect0.png"), rgb))
        self.assertEqual("mask", PIPELINE.category_for(Path("engine_lights.png"), rgb))
        self.assertEqual("normal", PIPELINE.category_for(Path("hull_normal.png"), rgb))

    def test_deterministic_processing_preserves_alpha_and_dimensions(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            source = root / "source" / "R1" / "Scout" / "lod0" / "hull_teamEffect0.png"
            output = root / "output"
            source.parent.mkdir(parents=True)
            image = Image.new("RGBA", (2, 3), (20, 40, 60, 0))
            image.putpixel((1, 2), (20, 40, 60, 255))
            image.save(source)
            jobs = PIPELINE.plan_jobs(root / "source", output, 4, 4096, "realesrgan")
            self.assertEqual(1, len(jobs))
            self.assertEqual("pillow", jobs[0].backend)
            result = PIPELINE.process_job(jobs[0], None, "realesrgan-x4plus", 256)
            self.assertEqual("complete", result.status)
            with Image.open(result.output) as generated:
                self.assertEqual((2, 3), generated.size)
                self.assertIn("A", generated.getbands())
                self.assertEqual(0, generated.getpixel((0, 0))[3])
                self.assertEqual(255, generated.getpixel((1, 2))[3])


if __name__ == "__main__":
    unittest.main()
