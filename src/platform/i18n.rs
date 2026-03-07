use std::sync::OnceLock;

static LOCALE: OnceLock<Locale> = OnceLock::new();

#[derive(Clone, Copy)]
pub enum Locale {
    En,
    Ko,
    Ja,
    Zh,
    Es,
}

#[derive(Clone, Copy)]
pub enum Key {
    InitSensor,
    SensorInitialized,
    SensorInitializedReady,
    WaitingFinger,
    FingerDetected,
    WaitingSensorScan,
    ScanTimeoutReadAnyway,
    CaptureRetrying,
    WaitRetrying,
    PowerOffHint,
    CaptureImage,
    SavedPath,
    TimeoutNoFinger,
    FallbackScanDetect,
    BuildLabel,
    ReadyLabel,
    FirmwareLabel,
    HardwareIdLabel,
    DeviceOpened,
    CaptureCompleteLine,
    AnalyzeTotalPackets,
    AnalyzeOutPackets,
    AnalyzeInPackets,
    AnalyzeOutSizes,
    AnalyzeInSizes,
    AnalyzeLargeInPackets,
    AnalyzeTrafficPhasesTitle,
    AnalyzeUniqueWritesTitle,
    AnalyzeUniqueReadsTitle,
    AnalyzeImageEventsTitle,
    AnalyzeTotalCaptures,
    ExtractFoundImages,
    ExtractImageHeader,
    ExtractRawSaved,
    ExtractPngSaved,
    ExtractPngSavedOff3,
    ExtractTotalImages,
    ExtractImageSizes,
    ExtractExpectedSize,
}

pub fn locale() -> Locale {
    *LOCALE.get_or_init(|| {
        let lang = std::env::var("LANG")
            .unwrap_or_else(|_| "en".to_string())
            .to_ascii_lowercase();
        if lang.starts_with("ko") {
            Locale::Ko
        } else if lang.starts_with("ja") {
            Locale::Ja
        } else if lang.starts_with("zh") {
            Locale::Zh
        } else if lang.starts_with("es") {
            Locale::Es
        } else {
            Locale::En
        }
    })
}

pub fn text(key: Key) -> &'static str {
    match (key, locale()) {
        (Key::InitSensor, Locale::Ko) => "[*] 센서를 초기화하는 중...",
        (Key::InitSensor, Locale::Ja) => "[*] センサーを初期化中...",
        (Key::InitSensor, Locale::Zh) => "[*] 正在初始化传感器...",
        (Key::InitSensor, Locale::Es) => "[*] Inicializando el sensor...",
        (Key::InitSensor, _) => "[*] Initializing sensor...",

        (Key::SensorInitialized, Locale::Ko) => "[OK] 센서 초기화 완료",
        (Key::SensorInitialized, Locale::Ja) => "[OK] センサー初期化完了",
        (Key::SensorInitialized, Locale::Zh) => "[OK] 传感器初始化完成",
        (Key::SensorInitialized, Locale::Es) => "[OK] Sensor inicializado",
        (Key::SensorInitialized, _) => "[OK] Sensor initialized",

        (Key::SensorInitializedReady, Locale::Ko) => "[OK] 센서 초기화 및 준비 완료.",
        (Key::SensorInitializedReady, Locale::Ja) => "[OK] センサー初期化と準備が完了しました。",
        (Key::SensorInitializedReady, Locale::Zh) => "[OK] 传感器初始化并准备就绪。",
        (Key::SensorInitializedReady, Locale::Es) => "[OK] Sensor inicializado y listo.",
        (Key::SensorInitializedReady, _) => "[OK] Sensor initialized and ready.",

        (Key::WaitingFinger, Locale::Ko) => "[*] 손가락을 기다리는 중...",
        (Key::WaitingFinger, Locale::Ja) => "[*] 指を待機中...",
        (Key::WaitingFinger, Locale::Zh) => "[*] 正在等待手指...",
        (Key::WaitingFinger, Locale::Es) => "[*] Esperando el dedo...",
        (Key::WaitingFinger, _) => "[*] Waiting for finger...",

        (Key::FingerDetected, Locale::Ko) => "[OK] 손가락 감지됨!",
        (Key::FingerDetected, Locale::Ja) => "[OK] 指を検出しました！",
        (Key::FingerDetected, Locale::Zh) => "[OK] 已检测到手指！",
        (Key::FingerDetected, Locale::Es) => "[OK] ¡Dedo detectado!",
        (Key::FingerDetected, _) => "[OK] Finger detected!",

        (Key::WaitingSensorScan, Locale::Ko) => "  센서 스캔 대기 중...",
        (Key::WaitingSensorScan, Locale::Ja) => "  センサースキャン待機中...",
        (Key::WaitingSensorScan, Locale::Zh) => "  正在等待传感器扫描...",
        (Key::WaitingSensorScan, Locale::Es) => "  Esperando escaneo del sensor...",
        (Key::WaitingSensorScan, _) => "  Waiting for sensor scan...",

        (Key::ScanTimeoutReadAnyway, Locale::Ko) => {
            "  스캔 시간 초과(무접촉 추정), 계속 읽기를 진행합니다..."
        }
        (Key::ScanTimeoutReadAnyway, Locale::Ja) => {
            "  スキャンがタイムアウトしました（未接触の可能性）。読み取りを続行します..."
        }
        (Key::ScanTimeoutReadAnyway, Locale::Zh) => {
            "  扫描超时（可能未接触），继续读取..."
        }
        (Key::ScanTimeoutReadAnyway, Locale::Es) => {
            "  Tiempo de escaneo agotado (¿sin dedo?), continuando de todos modos..."
        }
        (Key::ScanTimeoutReadAnyway, _) => "  Scan timeout (no finger?), reading anyway...",

        (Key::CaptureRetrying, Locale::Ko) => {
            "[!] 캡처 중 USB 오류({kind}). 장치를 다시 열고 1회 재시도합니다..."
        }
        (Key::CaptureRetrying, Locale::Ja) => {
            "[!] キャプチャ中にUSBエラー({kind})。デバイスを再オープンして1回再試行します..."
        }
        (Key::CaptureRetrying, Locale::Zh) => {
            "[!] 捕获时发生USB错误({kind})。正在重新打开设备并重试一次..."
        }
        (Key::CaptureRetrying, Locale::Es) => {
            "[!] Error USB durante la captura ({kind}). Reabriendo el dispositivo y reintentando una vez..."
        }
        (Key::CaptureRetrying, _) => {
            "[!] USB error during capture ({kind}). Reopening device and retrying once..."
        }

        (Key::WaitRetrying, Locale::Ko) => "[!] 손가락 대기 중 USB 오류({kind}). 1회 재시도합니다...",
        (Key::WaitRetrying, Locale::Ja) => {
            "[!] 指待機中にUSBエラー({kind})。1回再試行します..."
        }
        (Key::WaitRetrying, Locale::Zh) => "[!] 等待手指时发生USB错误({kind})。重试一次...",
        (Key::WaitRetrying, Locale::Es) => {
            "[!] Error USB mientras se esperaba el dedo ({kind}). Reintentando una vez..."
        }
        (Key::WaitRetrying, _) => "[!] USB error while waiting for finger ({kind}). Retrying once...",

        (Key::PowerOffHint, Locale::Ko) => {
            "[!] 재시도도 실패하면 노트북 전원을 30초 이상 완전히 끄고 다시 시도하세요."
        }
        (Key::PowerOffHint, Locale::Ja) => {
            "[!] 再試行でも失敗する場合は、ノートPCの電源を30秒以上完全に切ってください。"
        }
        (Key::PowerOffHint, Locale::Zh) => {
            "[!] 如果重试仍失败，请将笔记本完全断电30秒以上后再试。"
        }
        (Key::PowerOffHint, Locale::Es) => {
            "[!] Si vuelve a fallar, apaga completamente el portátil durante más de 30 segundos."
        }
        (Key::PowerOffHint, _) => {
            "[!] If retry fails again, fully power off the laptop for 30+ seconds."
        }

        (Key::CaptureImage, Locale::Ko) => "[*] 이미지 캡처 #{index}...",
        (Key::CaptureImage, Locale::Ja) => "[*] 画像キャプチャ #{index}...",
        (Key::CaptureImage, Locale::Zh) => "[*] 正在捕获图像 #{index}...",
        (Key::CaptureImage, Locale::Es) => "[*] Capturando imagen #{index}...",
        (Key::CaptureImage, _) => "[*] Capturing image #{index}...",

        (Key::SavedPath, Locale::Ko) => "[OK] 저장 완료: {path}",
        (Key::SavedPath, Locale::Ja) => "[OK] 保存完了: {path}",
        (Key::SavedPath, Locale::Zh) => "[OK] 已保存: {path}",
        (Key::SavedPath, Locale::Es) => "[OK] Guardado: {path}",
        (Key::SavedPath, _) => "[OK] Saved: {path}",

        (Key::TimeoutNoFinger, Locale::Ko) => {
            "[!] 시간 초과: {seconds}초 내에 손가락이 감지되지 않았습니다."
        }
        (Key::TimeoutNoFinger, Locale::Ja) => {
            "[!] タイムアウト: {seconds}秒以内に指を検出できませんでした。"
        }
        (Key::TimeoutNoFinger, Locale::Zh) => "[!] 超时：在 {seconds} 秒内未检测到手指。",
        (Key::TimeoutNoFinger, Locale::Es) => {
            "[!] Tiempo agotado: no se detectó dedo después de {seconds}s"
        }
        (Key::TimeoutNoFinger, _) => "[!] Timeout: no finger detected after {seconds}s",

        (Key::FallbackScanDetect, Locale::Ko) => "[*] 스캔 완료 기반 손가락 감지로 대체합니다...",
        (Key::FallbackScanDetect, Locale::Ja) => {
            "[*] スキャン完了ベースの指検出にフォールバックします..."
        }
        (Key::FallbackScanDetect, Locale::Zh) => "[*] 正在回退到基于扫描完成的手指检测...",
        (Key::FallbackScanDetect, Locale::Es) => {
            "[*] Cambiando a detección de dedo por finalización de escaneo..."
        }
        (Key::FallbackScanDetect, _) => "[*] Falling back to scan-complete finger detection...",

        (Key::BuildLabel, Locale::Ko) => "  빌드:",
        (Key::BuildLabel, Locale::Ja) => "  ビルド:",
        (Key::BuildLabel, Locale::Zh) => "  构建:",
        (Key::BuildLabel, Locale::Es) => "  Compilación:",
        (Key::BuildLabel, _) => "  Build:",

        (Key::ReadyLabel, Locale::Ko) => "  준비 상태:",
        (Key::ReadyLabel, Locale::Ja) => "  準備状態:",
        (Key::ReadyLabel, Locale::Zh) => "  就绪状态:",
        (Key::ReadyLabel, Locale::Es) => "  Estado listo:",
        (Key::ReadyLabel, _) => "  Ready:",

        (Key::FirmwareLabel, Locale::Ko) => "  펌웨어:",
        (Key::FirmwareLabel, Locale::Ja) => "  ファームウェア:",
        (Key::FirmwareLabel, Locale::Zh) => "  固件:",
        (Key::FirmwareLabel, Locale::Es) => "  Firmware:",
        (Key::FirmwareLabel, _) => "  Firmware:",

        (Key::HardwareIdLabel, Locale::Ko) => "  하드웨어 ID:",
        (Key::HardwareIdLabel, Locale::Ja) => "  ハードウェアID:",
        (Key::HardwareIdLabel, Locale::Zh) => "  硬件 ID:",
        (Key::HardwareIdLabel, Locale::Es) => "  ID de hardware:",
        (Key::HardwareIdLabel, _) => "  Hardware ID:",

        (Key::DeviceOpened, Locale::Ko) => "[OK] 장치 연결 성공: {vid}:{pid}",
        (Key::DeviceOpened, Locale::Ja) => "[OK] デバイス接続成功: {vid}:{pid}",
        (Key::DeviceOpened, Locale::Zh) => "[OK] 设备已连接: {vid}:{pid}",
        (Key::DeviceOpened, Locale::Es) => "[OK] Dispositivo abierto: {vid}:{pid}",
        (Key::DeviceOpened, _) => "[OK] Device opened: {vid}:{pid}",

        (Key::CaptureCompleteLine, Locale::Ko) => {
            "  이미지 캡처 완료: {width}x{height} ({bytes} bytes), 전송: {ms}ms"
        }
        (Key::CaptureCompleteLine, Locale::Ja) => {
            "  画像キャプチャ完了: {width}x{height} ({bytes} bytes), 転送: {ms}ms"
        }
        (Key::CaptureCompleteLine, Locale::Zh) => {
            "  图像捕获完成: {width}x{height} ({bytes} bytes), 传输: {ms}ms"
        }
        (Key::CaptureCompleteLine, Locale::Es) => {
            "  Captura completada: {width}x{height} ({bytes} bytes), transferencia: {ms}ms"
        }
        (Key::CaptureCompleteLine, _) => {
            "  Image captured: {width}x{height} ({bytes} bytes), transfer: {ms}ms"
        }

        (Key::AnalyzeTotalPackets, Locale::Ko) => "Total Bulk data packets: {count}",
        (Key::AnalyzeTotalPackets, Locale::Ja) => "Total Bulk data packets: {count}",
        (Key::AnalyzeTotalPackets, Locale::Zh) => "Total Bulk data packets: {count}",
        (Key::AnalyzeTotalPackets, Locale::Es) => "Total Bulk data packets: {count}",
        (Key::AnalyzeTotalPackets, _) => "Total Bulk data packets: {count}",
        (Key::AnalyzeOutPackets, Locale::Ko) => "  OUT (EP 0x01): {count}",
        (Key::AnalyzeOutPackets, _) => "  OUT (EP 0x01): {count}",
        (Key::AnalyzeInPackets, Locale::Ko) => "  IN  (EP 0x82): {count}",
        (Key::AnalyzeInPackets, _) => "  IN  (EP 0x82): {count}",
        (Key::AnalyzeOutSizes, Locale::Ko) => "OUT packet sizes: {sizes}",
        (Key::AnalyzeOutSizes, _) => "OUT packet sizes: {sizes}",
        (Key::AnalyzeInSizes, Locale::Ko) => "IN  packet sizes: {sizes}",
        (Key::AnalyzeInSizes, _) => "IN  packet sizes: {sizes}",
        (Key::AnalyzeLargeInPackets, Locale::Ko) => "Large IN packets (>10B): {count}",
        (Key::AnalyzeLargeInPackets, _) => "Large IN packets (>10B): {count}",
        (Key::AnalyzeTrafficPhasesTitle, Locale::Ko) => "TRAFFIC PHASES (separated by >0.5s gaps)",
        (Key::AnalyzeTrafficPhasesTitle, _) => "TRAFFIC PHASES (separated by >0.5s gaps)",
        (Key::AnalyzeUniqueWritesTitle, Locale::Ko) => "UNIQUE REGISTER WRITES",
        (Key::AnalyzeUniqueWritesTitle, _) => "UNIQUE REGISTER WRITES",
        (Key::AnalyzeUniqueReadsTitle, Locale::Ko) => "UNIQUE REGISTER READS",
        (Key::AnalyzeUniqueReadsTitle, _) => "UNIQUE REGISTER READS",
        (Key::AnalyzeImageEventsTitle, Locale::Ko) => "IMAGE CAPTURE EVENTS",
        (Key::AnalyzeImageEventsTitle, _) => "IMAGE CAPTURE EVENTS",
        (Key::AnalyzeTotalCaptures, Locale::Ko) => "  Total image captures: {count}",
        (Key::AnalyzeTotalCaptures, _) => "  Total image captures: {count}",

        (Key::ExtractFoundImages, Locale::Ko) => "Found {count} fingerprint images",
        (Key::ExtractFoundImages, _) => "Found {count} fingerprint images",
        (Key::ExtractImageHeader, Locale::Ko) => "  Image #{index}: frame={frame}, t={time}s, {bytes}B",
        (Key::ExtractImageHeader, _) => "  Image #{index}: frame={frame}, t={time}s, {bytes}B",
        (Key::ExtractRawSaved, Locale::Ko) => "    Raw saved: {path}",
        (Key::ExtractRawSaved, _) => "    Raw saved: {path}",
        (Key::ExtractPngSaved, Locale::Ko) => "    PNG saved: {path} ({width}x{height} 8-bit)",
        (Key::ExtractPngSaved, _) => "    PNG saved: {path} ({width}x{height} 8-bit)",
        (Key::ExtractPngSavedOff3, Locale::Ko) => "    PNG saved: {path} (3B offset)",
        (Key::ExtractPngSavedOff3, _) => "    PNG saved: {path} (3B offset)",
        (Key::ExtractTotalImages, Locale::Ko) => "Total images extracted: {count}",
        (Key::ExtractTotalImages, _) => "Total images extracted: {count}",
        (Key::ExtractImageSizes, Locale::Ko) => "Image sizes: {sizes}",
        (Key::ExtractImageSizes, _) => "Image sizes: {sizes}",
        (Key::ExtractExpectedSize, Locale::Ko) => "Expected for {width}x{height}@8bpp: {bytes}B",
        (Key::ExtractExpectedSize, _) => "Expected for {width}x{height}@8bpp: {bytes}B",
    }
}

pub fn format(key: Key, pairs: &[(&str, String)]) -> String {
    let mut out = text(key).to_string();
    for (k, v) in pairs {
        let pat = format!("{{{k}}}");
        out = out.replace(&pat, v);
    }
    out
}
