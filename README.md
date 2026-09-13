# 7-Segment Clock Reader dla OBS Studio

Wtyczka do OBS Studio, która w czasie rzeczywistym odczytuje zegar 7-segmentowy
(np. zegar meczowy z transmisji sportowej) z wybranego obszaru źródła wideo
i wyświetla wynik jako **zwykły tekst** — w dowolnym źródle tekstowym OBS.

Zamiast OCR używa deterministycznego dekodowania segmentów (bez modeli AI),
dlatego nie daje fałszywych odczytów. Zegar zatrzymany w trakcie meczu jest
poprawnie rozpoznawany — wtyczka trzyma ostatnią wartość i wznawia odczyt,
gdy zegar ruszy ponownie.

## Funkcje

- **Filtr wideo** nakładany na dowolne źródło (kamera, Media Source, stream) —
  nie modyfikuje obrazu, tylko z niego czyta.
- **Wybór obszaru (ROI)** z podglądu na żywo: przeciągnij ramkę, scroll = zoom,
  prawy przycisk = przesuwanie, auto-dopasowanie do ciemnego panelu zegara.
- **Wynik jako zwykły tekst**: filtr aktualizuje wybrany źródło typu
  *Tekst (FreeType 2)* / *Tekst (GDI+)* — możesz je stylizować, pozycjonować
  i używać w dowolnym miejscu nakładki.
- **Odporność na błędy**: walidacja segmentów + filtr czasowy — złe odczyty
  są odrzucane, zatrzymany zegar „trzyma” wartość, brak obrazu = ostatnia
  wartość (status LOST).
- **Opcje**: tylko sekundy (bez dziesiątek), kierunek odliczania
  (auto / w górę / w dół), auto-dopasowanie ROI.
- Obszar ROI skaluje się automatycznie przy zmianie rozdzielczości źródła.

## Wymagania

- OBS Studio **32.x** (x64) — zbudowane i testowane z OBS 32.2.2
- Windows 10/11 x64 (testowane)/ macOS 12+ Apple Silicon (patrz sekcja macOS)

## Instalacja (Windows)

1. Pobierz `seg7-clock-reader-1.0.0-windows-x64.zip` z sekcji
   [Releases](../../releases).
2. Wypakuj tak, aby folder `seg7-clock-reader` znalazł się w:
   `%APPDATA%\obs-studio\plugins\`
   (czyli `...\plugins\seg7-clock-reader\bin\64bit\seg7-clock-reader.dll`).
   Folder `plugins` utwórz, jeśli nie istnieje.
3. Uruchom ponownie OBS.

Alternatywnie (instalacja globalna, wymaga praw administratora): rozpakuj do
`C:\Program Files\obs-studio\` i scal foldery.

## Użycie

1. Dodaj źródło **Tekst (FreeType 2)** do sceny — to będzie twój zegar.
2. Dodaj źródło wideo (przechwycenie, Media Source, itp.).
3. Kliknij prawym na źródło wideo → **Filtry** → **+** → **7-Segment Clock Reader**.
4. W ustawieniach filtra:
   - **Źródło tekstu** — wybierz dodane wcześniej źródło tekstu,
   - **Wybierz ROI z wideo…** — przeciągnij ramkę wokół zegara
     (scroll = zoom, prawy przycisk = przesuwanie, dwuklik = reset);
     opcja *Auto-dopasuj ROI do panelu wyświetlacza* dociągnie zaznaczenie
     do ciemnego panelu zegara,
   - **Pokaż tylko sekundy** (zalecane), **Kierunek zegara** (Auto).
5. Gotowe — źródło tekstu aktualizuje się na żywo (1×/s lub 10×/s przy
   dziesiątych częściach sekundy), a podczas zatrzymania zegara trzyma
   ostatnią wartość.

## Budowanie ze źródeł

### Windows

Wymagane: Visual Studio 2022 (obciążenie „Desktop development with C++”),
CMake 3.28+, internet (zależności pobierane automatycznie).

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix release\RelWithDebInfo
```

Gotowy plugin: `release\RelWithDebInfo\obs-plugins\64bit\seg7-clock-reader.dll`
(+ `data\obs-plugins\seg7-clock-reader\`).

Alternatywnie skrypty CI:

```powershell
.\.github\scripts\Build-Windows.ps1   -Target x64 -Configuration RelWithDebInfo
.\.github\scripts\Package-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

Wynik: `release\seg7-clock-reader-1.0.0-windows-x64.zip`.

### macOS (Apple Silicon)

Wymagane: Xcode Command Line Tools, zainstalowany OBS z Homebrew.

```bash
make            # buduje build/seg7-clock-reader.plugin
make install    # instaluje do ~/Library/Application Support/obs-studio/plugins/
```

Makefile linkuje z frameworkami zainstalowanego OBS (`/Applications/OBS.app`),
nagłówki pobiera z odpowiadającego źródła obs-studio (automatycznie do `.deps/`).

### CI

Push do `main` buduje plugin dla Windows (GitHub Actions).
Tag w formacie `1.2.3` tworzy wydanie z gotowym zipem.

## Jak to działa

1. Filtr odbiera klatki źródła (płaszczyzna luminancji — bez konwersji kolorów).
2. ROI jest progowany, wyznaczane są glify (cyfry, kropki, separator),
   odrzucana ramka/bezel i śmieci.
3. Dla każdej cyfry sprawdzane jest pokrycie 7 segmentów (a–g) wzdłuż
   pochyłych osi; wynik musi pasować do wzorca cyfry z zapasem (margin).
4. Parser mapuje cyfry na czas `M:SS.t` i odrzuca niemożliwe wartości
   (np. sekundy ≥ 60).
5. Tracker czasowy odrzuca skoki/fałszywe odczyty, obsługuje kierunek,
   zatrzymania zegara (status STOPPED) i resynchronizację po ucięciach.

Rdzeń (`clock_core.cpp`) jest przenośnym C++17 bez zależności — ta sama logika
została zwalidowana 1:1 z referencyjną implementacją w Pythonie (720/720
klatek, 537/537 zdarzeń trackera).

## Struktura repozytorium

```
obs-plugin/
  src/            źródła pluginu (filtr, picker ROI, rdzeń dekodera)
  mac/            Info.plist + shim (build macOS)
  Makefile        build macOS (lokalny)
  .github/        workflow Windows + akcje CI (z obs-plugintemplate)
  BUILD-WINDOWS.txt  instrukcja budowania na Windows
cpp/              samodzielny harness CLI + testy zgodności z Pythonem
tools/            narzędzia (skan wideo, szukanie ROI, test e2e przez WebSocket)
seg7clock/        referencyjna implementacja w Pythonie (aplikacja desktopowa)
tests/            testy jednostkowe (fixtures + tracker + wideo end-to-end)
```

## Licencja

GPL-2.0 (dziedziczona z obs-plugintemplate).
