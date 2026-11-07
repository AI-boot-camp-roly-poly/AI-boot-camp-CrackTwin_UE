# CrackTwin (UE5)

디지털 트윈 기반 구조물 균열 관리 웹서비스의 UE5 프로젝트입니다. 가상 드론으로 건물을 점검하고, Pixel Streaming 2로 웹에 화면을 보내며, 점검 화면을 서버(`POST /inspections/{id}/detect`)에 올립니다.

## 요구 사항

- Unreal Engine **5.8**
- Visual Studio (C++ 게임 개발 워크로드, MSVC 14.50 — `.vsconfig`로 설치 구성을 가져올 수 있습니다)
- Git LFS (`.uasset`, `.umap`은 LFS로 저장되어 있습니다)

```powershell
git lfs install
git clone https://github.com/AI-boot-camp-roly-poly/-AI-boot-camp-CrackTwin_UE.git
```

## Fab 에셋을 따로 받아야 합니다

아래 마켓플레이스(Fab) 에셋은 라이선스상 재배포할 수 없고 용량(약 1.4GB)도 커서 저장소에 넣지 않았습니다. **프로젝트를 열기 전에 Fab에서 직접 받아 같은 경로에 추가하세요.** 없으면 레벨은 열리지만 건물·도로·지형 메시와 머티리얼, 균열 데칼이 비어 보입니다.

| 에셋 | 넣을 경로 |
|---|---|
| DynamicMeshCreator (도시 메시·머티리얼·텍스처) | `Content/DynamicMeshCreator/` |
| Megascans Surface: Dirty Concrete Floor | `Content/Fab/Megascans/Surfaces/` |
| Megascans Decal: Concrete Crack (sdnqgfi, sezrfcg, sfilvlg) | `Content/Fab/Megascans/Decals/` |
| Megascans Decal: Asphalt Crack, Concrete Patch, Rock Patch, Military Trenches Metal Rust 01 | `Content/Fab/Megascans/Decals/` |

- Fab 웹사이트나 에디터의 Fab 창에서 프로젝트에 추가합니다. Megascans를 추가하면 공용 머티리얼(`Content/Fab/Materials`, `MaterialFunctions`, `Textures` 등)도 함께 생깁니다.
- `Content/DynamicMeshCreator/Map/`의 레벨 파일(`Main`, `ChineseCity`)은 저장소에 들어 있습니다. 균열 데칼과 점검 위치를 이 레벨에 배치했으니 Fab에서 받은 파일로 **덮어쓰지 마세요.**

## 빌드와 실행

1. `CrackTwin.uproject`를 우클릭해 Visual Studio 프로젝트 파일을 생성하거나, 바로 열어 모듈 빌드를 허용합니다.
2. 기본 맵은 `/Game/DynamicMeshCreator/Map/Main`입니다.
3. 패키징한 실행 파일은 다음 인자로 실행합니다.

```
-PixelStreamingURL=ws://127.0.0.1:8888 -CrackApiUrl=<서버 주소> -ResX=1920 -ResY=1080
```

## 조작

W/S 앞뒤, A/D 좌우, E 상승, Q 하강, 마우스 시점.

## 저장소에 없는 것

- 문서(`*.md`, `docs/`) — 이 README만 포함합니다.
- 생성 폴더: `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`
- Pixel Streaming 시그널링 서버(`Tools/PixelStreaming/`): Epic PixelStreamingInfrastructure의 `UE5.8` 브랜치에서 받습니다.
