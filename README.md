# CrackTwin (UE5)

디지털 트윈 기반 구조물 균열 관리 웹서비스의 Unreal Engine 5 프로젝트입니다. 강원대학교 AI 아이디어콘 웹서비스 트랙 출품작입니다.

가상 도시에 균열이 있는 건물을 배치했습니다. 사용자는 웹 브라우저에서 Pixel Streaming으로 가상 드론을 조종해 건물을 살펴봅니다. 「점검」을 누르면 드론 카메라 화면과 촬영 위치를 서버로 보내고, AI가 균열을 찾아 결과를 3D 뷰어에 표시합니다.

## 이 저장소가 맡는 부분

- 가상 도시 레벨과 균열 데칼
- 가상 드론 (이동, 카메라, 촬영 위치 계산)
- Pixel Streaming 2로 웹에 화면 전송, 웹에서 받은 점검 요청 처리
- 점검 화면을 AI 서버에 업로드하고 결과를 웹에 전달

웹 프론트엔드, AI 서버, DB는 다른 저장소에 있습니다.

## 요구 사항

- Unreal Engine **5.8**
- Visual Studio (C++ 게임 개발 워크로드, MSVC 14.50 — `.vsconfig`로 설치 구성을 가져올 수 있습니다)
- Git LFS (`.uasset`, `.umap`은 LFS로 저장되어 있습니다)

```powershell
git lfs install
git clone https://github.com/AI-boot-camp-roly-poly/AI-boot-camp-CrackTwin_UE.git
```

## Fab 에셋을 따로 받아야 합니다

아래 마켓플레이스(Fab) 에셋은 라이선스상 재배포할 수 없어 저장소에 넣지 않았습니다. **프로젝트를 열기 전에 Fab에서 직접 받아 같은 경로에 추가하세요.** 없으면 레벨은 열리지만 건물·도로·지형 메시와 머티리얼, 균열 데칼이 비어 보입니다.

| 에셋 | 넣을 경로 |
|---|---|
| DynamicMeshCreator (도시 메시·머티리얼·텍스처) | `Content/DynamicMeshCreator/` |
| Megascans Surface: Dirty Concrete Floor | `Content/Fab/Megascans/Surfaces/` |
| Megascans Decal: Concrete Crack (sdnqgfi, sezrfcg, sfilvlg) | `Content/Fab/Megascans/Decals/` |
| Megascans Decal: Asphalt Crack, Concrete Patch, Rock Patch, Military Trenches Metal Rust 01 | `Content/Fab/Megascans/Decals/` |

- Fab 웹사이트나 에디터의 Fab 창에서 프로젝트에 추가합니다. Megascans를 추가하면 공용 머티리얼(`Content/Fab/Materials` 등)도 함께 생깁니다.
- `Content/DynamicMeshCreator/Map/`의 레벨 파일(`Main`, `ChineseCity`)은 저장소에 들어 있습니다. 균열 데칼과 점검 위치를 이 레벨에 배치했으니 Fab에서 받은 파일로 **덮어쓰지 마세요.**

## 빌드와 실행

1. `CrackTwin.uproject`를 열고 모듈 빌드를 허용합니다. Visual Studio에서 빌드하려면 `.uproject`를 우클릭해 프로젝트 파일을 생성합니다.
2. 기본 맵은 `/Game/DynamicMeshCreator/Map/Main`입니다.
3. 패키징한 실행 파일은 다음 인자로 실행합니다. `-CrackApiUrl`에는 AI 서버 주소를 넣습니다.

```
-PixelStreamingURL=ws://127.0.0.1:8888 -CrackApiUrl=<서버 주소> -ResX=1920 -ResY=1080
```

웹에서 화면을 보려면 Epic의 Pixel Streaming 시그널링 서버(PixelStreamingInfrastructure, `UE5.8` 브랜치)를 함께 실행해야 합니다.

## 조작

W/S 앞뒤, A/D 좌우, E 상승, Q 하강, 마우스 시점.

## 참고

좌표와 균열 크기(mm)는 가상 모델 기준 값이고, 위험도는 참고 정보입니다.
