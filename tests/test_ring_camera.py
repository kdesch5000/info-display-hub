"""
Tests for Ring Camera snapshot feature in info_display_hub.ino

Since this is an Arduino/ESP32 project, we can't run the C++ code directly.
These tests validate the logic and configuration consistency by parsing the
source file and testing the extractable logic patterns in Python equivalents.
"""

import re
import pytest
from pathlib import Path

INO_PATH = Path(__file__).parent.parent / "info_display_hub" / "info_display_hub.ino"


@pytest.fixture
def ino_source():
    """Read the .ino source file."""
    return INO_PATH.read_text()


# ============================================================
# Source Consistency Tests
# ============================================================

class TestConfigConsistency:
    """Verify all config plumbing is wired up for scr_ring."""

    def test_config_struct_has_screen_ring(self, ino_source):
        assert re.search(r'bool\s+screen_ring;', ino_source), \
            "Config struct must have bool screen_ring field"

    def test_load_config_reads_scr_ring(self, ino_source):
        assert re.search(r'prefs\.getBool\("scr_ring"', ino_source), \
            "loadConfig must read scr_ring from Preferences"

    def test_save_config_writes_scr_ring(self, ino_source):
        assert re.search(r'prefs\.putBool\("scr_ring"', ino_source), \
            "saveConfig must write scr_ring to Preferences"

    def test_json_get_includes_scr_ring(self, ino_source):
        assert re.search(r'doc\["scr_ring"\]\s*=\s*config\.screen_ring', ino_source), \
            "GET /api/config must include scr_ring in JSON response"

    def test_json_post_reads_scr_ring(self, ino_source):
        assert re.search(r'config\.screen_ring\s*=\s*obj\["scr_ring"\]', ino_source), \
            "POST /api/config must read scr_ring from JSON body"

    def test_html_has_scr_ring_checkbox(self, ino_source):
        assert re.search(r'id="scr_ring"', ino_source), \
            "CONFIG_HTML must have checkbox with id scr_ring"

    def test_js_load_sets_scr_ring(self, ino_source):
        assert re.search(r"getElementById\('scr_ring'\)\.checked\s*=", ino_source), \
            "JavaScript load must set scr_ring checkbox state"

    def test_js_submit_reads_scr_ring(self, ino_source):
        assert re.search(r"scr_ring:\s*document\.getElementById\('scr_ring'\)\.checked", ino_source), \
            "JavaScript submit must read scr_ring checkbox value"

    def test_all_screens_array_has_ring(self, ino_source):
        assert re.search(r'drawScreenRingCam.*screen_ring', ino_source), \
            "allScreens[] must register drawScreenRingCam with screen_ring"


class TestPreferencesKeyLength:
    """ESP32 Preferences keys must be <= 15 characters."""

    def test_scr_ring_key_length(self):
        assert len("scr_ring") <= 15


# ============================================================
# Camera Entity Tests
# ============================================================

class TestCameraEntities:
    """Validate camera entity configuration."""

    def test_entity_count_matches_constant(self, ino_source):
        entities = re.findall(r'"camera\.\w+_snapshot"', ino_source)
        count_match = re.search(r'const int RING_CAM_COUNT\s*=\s*(\d+);', ino_source)
        assert count_match, "RING_CAM_COUNT must be defined"
        assert int(count_match.group(1)) == len(entities), \
            f"RING_CAM_COUNT ({count_match.group(1)}) must match entity count ({len(entities)})"

    def test_names_count_matches_entities(self, ino_source):
        entities = re.findall(r'"camera\.\w+_snapshot"', ino_source)
        # Count quoted strings in RING_CAM_NAMES array
        names_block = re.search(
            r'RING_CAM_NAMES\[\]\s*=\s*\{([^}]+)\}', ino_source
        )
        assert names_block, "RING_CAM_NAMES array must be defined"
        names = re.findall(r'"[^"]+"', names_block.group(1))
        assert len(names) == len(entities), \
            f"RING_CAM_NAMES count ({len(names)}) must match entity count ({len(entities)})"

    def test_entities_are_snapshot_type(self, ino_source):
        """All entities in the array should be snapshot entities, not live_view."""
        entities_block = re.search(
            r'RING_CAM_ENTITIES\[\]\s*=\s*\{([^}]+)\}', ino_source
        )
        assert entities_block
        entities = re.findall(r'"(camera\.[^"]+)"', entities_block.group(1))
        for entity in entities:
            assert "snapshot" in entity, \
                f"Entity {entity} should be a snapshot entity, not live_view"

    def test_no_living_room_camera(self, ino_source):
        """Living room camera was explicitly excluded by user."""
        entities_block = re.search(
            r'RING_CAM_ENTITIES\[\]\s*=\s*\{([^}]+)\}', ino_source
        )
        assert entities_block
        assert "living_room" not in entities_block.group(1), \
            "Living room camera must not be in RING_CAM_ENTITIES (excluded by user)"

    def test_expected_cameras_present(self, ino_source):
        """Verify the 5 expected cameras are all present."""
        entities_block = re.search(
            r'RING_CAM_ENTITIES\[\]\s*=\s*\{([^}]+)\}', ino_source
        )
        assert entities_block
        block = entities_block.group(1)
        expected = [
            "camera.front_door_snapshot",
            "camera.basement_door_snapshot",
            "camera.alley_garage_door_snapshot",
            "camera.gangway_snapshot",
            "camera.backyard_deck_snapshot",
        ]
        for entity in expected:
            assert entity in block, f"Expected entity {entity} not found in RING_CAM_ENTITIES"


# ============================================================
# Camera Cycling Logic Tests (Python equivalent)
# ============================================================

class TestCameraCyclingLogic:
    """Test the camera round-robin cycling logic (Python port of C++ logic)."""

    RING_CAM_COUNT = 5

    def cycle_camera(self, current):
        """Python equivalent of: (currentCam + 1) % RING_CAM_COUNT"""
        return (current + 1) % self.RING_CAM_COUNT

    def test_cycles_from_zero(self):
        assert self.cycle_camera(0) == 1

    def test_cycles_through_all(self):
        cam = 0
        visited = set()
        for _ in range(self.RING_CAM_COUNT):
            visited.add(cam)
            cam = self.cycle_camera(cam)
        assert visited == set(range(self.RING_CAM_COUNT)), \
            "Cycling must visit all cameras exactly once"

    def test_wraps_around(self):
        assert self.cycle_camera(self.RING_CAM_COUNT - 1) == 0, \
            "Must wrap from last camera back to first"

    def test_full_cycle_returns_to_start(self):
        cam = 0
        for _ in range(self.RING_CAM_COUNT):
            cam = self.cycle_camera(cam)
        assert cam == 0, "After RING_CAM_COUNT cycles, must return to camera 0"


# ============================================================
# URL Construction Tests (Python equivalent)
# ============================================================

class TestURLConstruction:
    """Test HA camera_proxy URL construction logic."""

    ENTITIES = [
        "camera.front_door_snapshot",
        "camera.basement_door_snapshot",
        "camera.alley_garage_door_snapshot",
        "camera.gangway_snapshot",
        "camera.backyard_deck_snapshot",
    ]

    def build_url(self, ha_url, entity_idx):
        """Python equivalent of the URL construction in fetchRingSnapshot."""
        return f"{ha_url}/api/camera_proxy/{self.ENTITIES[entity_idx]}"

    def test_url_format(self):
        url = self.build_url("http://home.local:8123", 0)
        assert url == "http://home.local:8123/api/camera_proxy/camera.front_door_snapshot"

    def test_url_no_trailing_slash(self):
        url = self.build_url("http://ha:8123", 2)
        assert "//" not in url.split("://")[1], "URL should not have double slashes in path"

    def test_all_entities_produce_valid_urls(self):
        for i in range(len(self.ENTITIES)):
            url = self.build_url("http://ha:8123", i)
            assert url.startswith("http://ha:8123/api/camera_proxy/camera.")
            assert url.endswith("_snapshot")


# ============================================================
# Index Display Format Tests
# ============================================================

class TestIndexDisplay:
    """Test the camera index indicator formatting (e.g. '3/5')."""

    RING_CAM_COUNT = 5

    def format_index(self, current_cam):
        """Python equivalent of: snprintf(idxBuf, sizeof(idxBuf), "%d/%d", currentCam + 1, RING_CAM_COUNT)"""
        return f"{current_cam + 1}/{self.RING_CAM_COUNT}"

    def test_first_camera(self):
        assert self.format_index(0) == "1/5"

    def test_last_camera(self):
        assert self.format_index(4) == "5/5"

    def test_middle_camera(self):
        assert self.format_index(2) == "3/5"

    def test_all_indices_are_valid(self):
        for i in range(self.RING_CAM_COUNT):
            result = self.format_index(i)
            num, denom = result.split("/")
            assert 1 <= int(num) <= self.RING_CAM_COUNT
            assert int(denom) == self.RING_CAM_COUNT


# ============================================================
# JPEG Callback Clipping Logic Tests (Python equivalent)
# ============================================================

class TestJpgClipping:
    """Test the JPEG decode callback clipping logic.

    The callback offsets y by -5 to center the 180px decoded image
    in the 170px display, cropping 5px from top and 5px from bottom.
    """

    SCREEN_HEIGHT = 170
    Y_OFFSET = -5  # adjustedY = y - 5

    def should_draw_pixel(self, block_y, row, block_h):
        """Python equivalent of the clipping logic in jpgDrawCallback."""
        adjusted_y = block_y + self.Y_OFFSET
        screen_y = adjusted_y + row
        return 0 <= screen_y < self.SCREEN_HEIGHT

    def test_top_crop_pixels_skipped(self):
        """First 5 pixel rows of decoded image should be cropped."""
        for row in range(5):
            assert not self.should_draw_pixel(0, row, 8), \
                f"Pixel at decoded row {row} should be cropped (above display)"

    def test_first_visible_row(self):
        """Row 5 of decoded image maps to screen row 0."""
        assert self.should_draw_pixel(0, 5, 8)

    def test_bottom_crop(self):
        """Pixels beyond screen height should be cropped."""
        # Decoded height is 180, screen is 170, offset is -5
        # Decoded row 175 -> screen row 170 -> out of bounds
        assert not self.should_draw_pixel(170, 5, 8)

    def test_last_visible_row(self):
        """Screen row 169 (last row) should be visible."""
        # decoded y=174, offset=-5 -> screen y=169
        assert self.should_draw_pixel(174, 0, 1)

    def test_block_fully_above_screen(self):
        """Block entirely above the display should be skipped."""
        adjusted_y = 0 + self.Y_OFFSET  # -5
        block_h = 4
        # All rows: -5, -4, -3, -2 -> all < 0
        for row in range(block_h):
            assert not self.should_draw_pixel(0, row, block_h)

    def test_block_partially_visible(self):
        """Block spanning the top edge should have some pixels drawn."""
        # Block at decoded y=0, h=8: adjusted_y=-5, rows 0-7 -> screen -5 to 2
        visible = [self.should_draw_pixel(0, row, 8) for row in range(8)]
        assert any(visible), "Some pixels should be visible"
        assert not all(visible), "Not all pixels should be visible (top is cropped)"


# ============================================================
# PSRAM Buffer Size Test
# ============================================================

class TestBufferSizing:
    """Verify buffer is large enough for expected JPEG sizes."""

    def test_buffer_larger_than_typical_snapshot(self, ino_source):
        """The PSRAM buffer (64KB) should be larger than typical Ring snapshots (~42KB)."""
        match = re.search(r'ps_malloc\((\d+)\)', ino_source)
        assert match, "ps_malloc call must exist for JPEG buffer"
        buffer_size = int(match.group(1))
        assert buffer_size >= 65536, f"Buffer size {buffer_size} should be at least 64KB"
        typical_jpeg = 42000  # typical Ring snapshot
        assert buffer_size > typical_jpeg, \
            f"Buffer ({buffer_size}) must be larger than typical JPEG ({typical_jpeg})"

    def test_http_timeout_is_sufficient(self, ino_source):
        """Camera proxy requests need a longer timeout than regular API calls."""
        # Find the timeout in fetchRingSnapshot
        ring_section = ino_source[ino_source.index("fetchRingSnapshot"):]
        ring_section = ring_section[:ring_section.index("http.end()")]
        timeout_match = re.search(r'http\.setTimeout\((\d+)\)', ring_section)
        assert timeout_match, "fetchRingSnapshot must set HTTP timeout"
        timeout_ms = int(timeout_match.group(1))
        assert timeout_ms >= 8000, \
            f"Camera proxy timeout ({timeout_ms}ms) should be >= 8000ms"


# ============================================================
# TJpg_Decoder Configuration Tests
# ============================================================

class TestJpgDecoderSetup:
    """Verify TJpg_Decoder is properly configured."""

    def test_scale_is_set(self, ino_source):
        match = re.search(r'TJpgDec\.setJpgScale\((\d+)\)', ino_source)
        assert match, "TJpgDec.setJpgScale must be called in setup()"
        scale = int(match.group(1))
        assert scale == 2, f"Scale should be 2 (1/2) for 640->320, got {scale}"

    def test_callback_is_set(self, ino_source):
        assert "TJpgDec.setCallback(jpgDrawCallback)" in ino_source, \
            "TJpgDec callback must be set to jpgDrawCallback"

    def test_callback_function_exists(self, ino_source):
        assert re.search(r'bool jpgDrawCallback\(', ino_source), \
            "jpgDrawCallback function must be defined"

    def test_include_tjpg_decoder(self, ino_source):
        assert "#include <TJpg_Decoder.h>" in ino_source, \
            "TJpg_Decoder.h must be included"


# ============================================================
# Fetch Interval Tests
# ============================================================

class TestFetchIntervals:
    """Verify fetch interval is reasonable."""

    def test_ring_cam_interval_defined(self, ino_source):
        match = re.search(r'RING_CAM_INTERVAL\s*=\s*(\d+)', ino_source)
        assert match, "RING_CAM_INTERVAL must be defined"
        interval_ms = int(match.group(1))
        assert 10000 <= interval_ms <= 120000, \
            f"Ring cam interval ({interval_ms}ms) should be 10-120 seconds"

    def test_ring_fetch_guarded_by_screen_enabled(self, ino_source):
        """Ring camera fetch should only happen if screen is enabled."""
        assert re.search(r'config\.screen_ring', ino_source), \
            "Ring cam fetch must reference config.screen_ring"

    def test_ring_fetch_guarded_by_wifi(self, ino_source):
        """Ring fetch block should be inside the WiFi connected check."""
        # Find the WiFi connected block and verify ring fetch is inside it
        wifi_block_start = ino_source.index("if (WiFi.status() == WL_CONNECTED)")
        draw_screen = ino_source.index("// --- Draw current screen ---")
        ring_fetch = ino_source.index("config.screen_ring", wifi_block_start)
        assert wifi_block_start < ring_fetch < draw_screen, \
            "Ring cam fetch must be inside the WiFi.connected block, before draw"


# ============================================================
# Now Brewing Screen Tests
# ============================================================

class TestBrewingConfigConsistency:
    """Verify all config plumbing is wired up for the Now Brewing screen."""

    def test_config_struct_has_screen_brewing(self, ino_source):
        assert re.search(r'bool\s+screen_brewing;', ino_source)

    def test_config_struct_has_coffee_name(self, ino_source):
        assert re.search(r'char\s+coffee_name\[', ino_source)

    def test_config_struct_has_coffee_img(self, ino_source):
        assert re.search(r'char\s+coffee_img\[', ino_source)

    def test_load_config_reads_scr_brew(self, ino_source):
        assert re.search(r'prefs\.getBool\("scr_brew"', ino_source)

    def test_load_config_reads_coffee_name(self, ino_source):
        assert re.search(r'prefs\.getString\("cof_name"', ino_source)

    def test_load_config_reads_coffee_img(self, ino_source):
        assert re.search(r'prefs\.getString\("cof_img"', ino_source)

    def test_save_config_writes_all_fields(self, ino_source):
        assert re.search(r'prefs\.putBool\("scr_brew"', ino_source)
        assert re.search(r'prefs\.putString\("cof_name"', ino_source)
        assert re.search(r'prefs\.putString\("cof_img"', ino_source)

    def test_json_get_includes_brewing_fields(self, ino_source):
        assert re.search(r'doc\["scr_brewing"\]', ino_source)
        assert re.search(r'doc\["coffee_name"\]', ino_source)
        assert re.search(r'doc\["coffee_img"\]', ino_source)

    def test_json_post_reads_brewing_fields(self, ino_source):
        assert re.search(r'config\.screen_brewing\s*=\s*obj\["scr_brewing"\]', ino_source)
        assert re.search(r'config\.coffee_name.*obj\["coffee_name"\]', ino_source)
        assert re.search(r'config\.coffee_img.*obj\["coffee_img"\]', ino_source)

    def test_html_has_brewing_toggle(self, ino_source):
        assert re.search(r'id="scr_brewing"', ino_source)

    def test_html_has_coffee_name_input(self, ino_source):
        assert re.search(r'id="coffee_name"', ino_source)

    def test_html_has_coffee_img_input(self, ino_source):
        assert re.search(r'id="coffee_img"', ino_source)

    def test_js_load_sets_brewing_fields(self, ino_source):
        assert re.search(r"getElementById\('scr_brewing'\)\.checked", ino_source)
        assert re.search(r"getElementById\('coffee_name'\)\.value", ino_source)
        assert re.search(r"getElementById\('coffee_img'\)\.value", ino_source)

    def test_js_submit_reads_brewing_fields(self, ino_source):
        assert re.search(r"scr_brewing:\s*document", ino_source)
        assert re.search(r"coffee_name:\s*document", ino_source)
        assert re.search(r"coffee_img:\s*document", ino_source)

    def test_all_screens_array_has_brewing(self, ino_source):
        assert re.search(r'drawScreenBrewing.*screen_brewing', ino_source)


class TestBrewingPreferencesKeys:
    """ESP32 Preferences keys must be <= 15 characters."""

    def test_key_lengths(self):
        for key in ["scr_brew", "cof_name", "cof_img"]:
            assert len(key) <= 15, f"Key '{key}' is {len(key)} chars, max 15"


class TestBrewingImageFormat:
    """Verify PNG and JPEG support."""

    def test_pngdec_included(self, ino_source):
        assert "#include <PNGdec.h>" in ino_source

    def test_png_callback_exists(self, ino_source):
        assert re.search(r'int pngDrawCallback\(PNGDRAW', ino_source)

    def test_png_magic_detection(self, ino_source):
        """Format detection by magic bytes (PNG: 89 50 4E 47)."""
        assert '0x89' in ino_source and '0x50' in ino_source and '0x4E' in ino_source and '0x47' in ino_source, \
            "PNG magic bytes must be checked for format detection"

    def test_https_support(self, ino_source):
        """HTTPS support for roaster websites."""
        assert "WiFiClientSecure" in ino_source
        assert "setInsecure" in ino_source

    def test_redirect_support(self, ino_source):
        assert "setFollowRedirects" in ino_source
