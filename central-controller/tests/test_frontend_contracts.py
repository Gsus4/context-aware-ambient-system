from pathlib import Path


def test_fan_change_handler_does_not_drop_commands_when_smart_mode_is_on():
    source = Path("src/smart_space/web/dashboard/js/data-adapter.js").read_text(encoding="utf-8")

    assert "if (fanRange.disabled || smartModeEnabled()) return;" not in source
    assert "sendDashboardCommand('set_smart_mode', { enabled: false" in source
    assert "sendDashboardCommand('set_fan_level', { fanLevel, fanSpeed: fanLevel });" in source


def test_fan_control_lock_does_not_depend_on_smart_mode():
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "setControlLocked($('#fanRange'),!fanOnline||smartLocked);" not in source
    assert "setControlLocked($('#fanRange'),!fanOnline);" in source


def test_audio_controls_use_music_command_path_in_real_mode():
    adapter = Path("src/smart_space/web/dashboard/js/data-adapter.js").read_text(encoding="utf-8")
    app = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "sendMusicCommand(action" in adapter
    assert "data-audio-action" in adapter
    assert "audioAction(ab.dataset.audioAction)" not in app


def test_audio_progress_is_disabled_for_backend_v1():
    app = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")
    adapter = Path("src/smart_space/web/dashboard/js/data-adapter.js").read_text(encoding="utf-8")

    assert "setControlLocked($('#audioProgressRange'),true);" in app
    assert "set_audio_progress" not in adapter


def test_audio_controls_do_not_depend_on_smart_mode_lock():
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "setManyLocked('.audio-btn, #audioVolumeRange',!audioOnline||smartLocked);" not in source
    assert "setManyLocked('.audio-btn, #audioVolumeRange',!audioOnline);" in source


def test_audio_module_is_represented_in_frontend_module_status():
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "{key:'audio',label:'音樂播放服務'}" in source


def test_real_music_backend_does_not_use_demo_audio_playlist_progress():
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "const realAudioBackend=" in source
    assert "if(realAudioBackend)return;" in source


def test_audio_track_event_is_emitted_only_when_audio_state_changes():
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "const previousTrack=state.audioState.track" in source
    assert "const trackChanged=o.track&&o.track!==previousTrack" in source
    assert "if(trackChanged||playbackChanged)" in source


def test_audio_panel_has_current_filename_display():
    html = Path("src/smart_space/web/dashboard/index.html").read_text(encoding="utf-8")
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")
    css = Path("src/smart_space/web/dashboard/css/dashboard.css").read_text(encoding="utf-8")

    assert 'id="musicMarqueeText"' in html
    assert 'class="audio-mode-line audio-current-track-line"' in html
    assert "function audioFileName" in source
    assert "audioFileName(a)" in source
    assert ".audio-current-track-line" in css
    assert "text-overflow:ellipsis" in css
    assert "animation:none" in css
    assert "width:190px" in css
    assert ".audio-panel-unified .audio-progress {\n  display: none !important;" in css


def test_audio_panel_desktop_row_height_is_not_left_at_legacy_compact_size():
    css = Path("src/smart_space/web/dashboard/css/dashboard.css").read_text(encoding="utf-8")

    assert "/* Final desktop row balance: give the audio pane enough height for filename + volume controls. */" in css
    assert "grid-template-rows: minmax(0, 1fr) 172px 174px !important;" in css
    assert "grid-template-rows: minmax(0, 1fr) 156px 162px !important;" in css


def test_scene_mode_defaults_do_not_overwrite_fan_state():
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert "fanSpeed:m.fanSpeed" not in source
    assert "fanOn:true" not in source


def test_camera_modal_uses_webrtc_first_with_hls_and_snapshot_fallback():
    html = Path("src/smart_space/web/dashboard/index.html").read_text(encoding="utf-8")
    source = Path("src/smart_space/web/dashboard/js/dashboard-app.js").read_text(encoding="utf-8")

    assert 'id="cameraWebrtcFrame"' in html
    assert 'id="cameraVideo"' in html
    assert 'playsinline' in html
    assert 'controls' not in html.split('id="cameraVideo"', 1)[1].split('>', 1)[0]
    assert 'disablepictureinpicture' in html
    assert 'hls.min.js' in html
    assert "function getCameraWebrtcUrl" in source
    assert "function getCameraStreamUrl" in source
    assert "function destroyCameraStream" in source
    assert "function loadCameraWebrtcFrame" in source
    assert "function loadCameraStream" in source
    assert "const webRtcFrame=$('#cameraWebrtcFrame');" in source
    assert "const video=$('#cameraVideo');" in source
    assert "video.controls=false;" in source
    assert "window.Hls&&window.Hls.isSupported()" in source
    assert "maxBufferLength:1" in source
    assert "liveMaxLatencyDurationCount:2" in source
    assert "maxLiveSyncPlaybackRate:1.5" in source
    assert "hls.liveSyncPosition" in source
    assert "return resolveCameraUrl(state.sensorData.cameraWebrtcUrl" in source
    assert "return state.sensorData.cameraHlsUrl||'/api/camera/hls/" in source
    assert "const img=$('#cameraSnapshot');" in source
