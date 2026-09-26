# Czytnik zegara 7-segmentowego do OBS Studio

Wtyczka odczytuje czas z zegara widocznego w filmie lub obrazie z kamery i wpisuje go do wybranego źródła tekstowego w OBS. Dzięki temu możesz pokazać czas meczu własną czcionką, w dowolnym miejscu transmisji.

Zegar powinien mieć jasne cyfry złożone z segmentów, jak w zegarku elektronicznym, na ciemnym tle. Zaznacz **cały czas z minutami i sekundami**, np. `1:23`, `12:34` lub `1:23.4`. Sam odczyt `5.2` albo zwykły napis drukowaną czcionką nie wystarczy.

Wtyczka nie zmienia obrazu wideo. Gdy zegar się zatrzyma lub zniknie z obrazu, pozostawia ostatni odczyt. Po powrocie czytelnego zegara wznawia aktualizację.

## Czego potrzebujesz

- **OBS Studio 32.x**, w wersji 64-bitowej.
- Na Windows: system Windows 10 lub 11 i paczka wtyczki dla Windows x64.
- Źródło **Multimedia** z odtwarzanym filmem albo **Urządzenie do przechwytywania wideo**, np. kamera lub karta przechwytująca.
- Źródło **Tekst (GDI+)** lub **Tekst (FreeType 2)**, w którym pojawi się odczytany czas.

Filtr nie obsługuje bezpośrednio źródeł **Przechwytywanie ekranu**, **Przechwytywanie okna**, **Przechwytywanie gry** ani **Przeglądarka**. Jeśli nie widzi obrazu, najpierw sprawdź rodzaj źródła.

Na macOS wtyczkę można zbudować ze źródeł. Instrukcja dla osób budujących wtyczkę znajduje się na końcu tej strony.

## Instalacja na Windows

1. Zamknij OBS.
2. Otwórz [stronę wydań](https://github.com/drajvver/seg7-clock-reader-obs/releases) i pobierz paczkę, której nazwa kończy się na **`windows-x64.zip`**. Nie wybieraj archiwum z kodem źródłowym.
3. Rozpakuj pobrany plik. W środku znajdziesz folder **`seg7-clock-reader`**.
4. Naciśnij **Win + R**, wklej poniższą ścieżkę i naciśnij Enter:

   ```text
   %APPDATA%\obs-studio\plugins
   ```

   Jeśli folder `plugins` nie istnieje, otwórz `%APPDATA%\obs-studio` i utwórz w nim folder o nazwie `plugins`.
5. Skopiuj do niego cały folder **`seg7-clock-reader`** i uruchom OBS.

Po skopiowaniu plik wtyczki powinien znajdować się tutaj:

```text
%APPDATA%\obs-studio\plugins\seg7-clock-reader\bin\64bit\seg7-clock-reader.dll
```

Nie twórz drugiego folderu `seg7-clock-reader` wewnątrz pierwszego. Przy aktualizacji zamknij OBS i zastąp dotychczasowy folder wtyczki nowym.

## Pierwsze uruchomienie

1. W panelu **Źródła** kliknij **+** i dodaj **Tekst (GDI+)** albo **Tekst (FreeType 2)**. Nazwij go np. **Czas meczu**. Wyłącz **Czytaj z pliku**, jeśli ta opcja jest dostępna; w FreeType 2 wybierz sposób wprowadzania tekstu **Własne**.
2. Dodaj lub włącz źródło z filmem albo kamerą. Sprawdź, czy zegar jest widoczny w podglądzie OBS.
3. Kliknij to **źródło wideo** prawym przyciskiem myszy i wybierz **Filtry**. Pod listą **Filtry audio/wideo** kliknij **+** i wybierz **Czytnik zegara 7-segmentowego**. Filtr należy do źródła wideo, a nie do źródła tekstowego.
4. W polu **Gdzie wyświetlić czas?** wybierz **Czas meczu**.
5. Kliknij **Zaznacz zegar na obrazie…**. Przeciągnij lewym przyciskiem ramkę wokół minut, sekund i separatorów. Pozostaw mały margines wokół cyfr. Nie obejmuj wyniku meczu ani innych napisów.
6. Kliknij **Użyj tego obszaru**. Po rozpoznaniu czasu pojawi się on w źródle **Czas meczu**. Ustaw jego czcionkę, kolor, rozmiar i położenie tak jak dla każdego tekstu w OBS.

W starszych wydaniach filtr może jeszcze występować pod nazwą **7-Segment Clock Reader**. Ta instrukcja opisuje interfejs bieżącego kodu.

### Jak zaznaczyć mały zegar

- **Kółko myszy** — przybliż lub oddal obraz.
- **Prawy przycisk i przeciągnięcie** — przesuń obraz.
- **Dwuklik lewym przyciskiem** — wróć do widoku całej klatki.
- **Anuluj** — zamknij okno bez zmiany zaznaczenia.

Okno pokazuje zatrzymaną klatkę, żeby łatwiej było zaznaczyć cyfry. Po zatwierdzeniu wtyczka odczytuje kolejne klatki filmu lub kamery.

## Ustawienia, które warto znać

| Ustawienie | Co robi i kiedy je zmienić |
| --- | --- |
| **Ukryj dziesiąte części sekundy** | Pokazuje np. `1:23` zamiast `1:23.4`. Domyślnie włączone. Minuty pozostają widoczne. |
| **Jak zmienia się czas?** | Zwykle pozostaw **Automatycznie**. Jeśli wiesz, że zegar zawsze odlicza do zera, możesz wybrać **Czas maleje**. |
| **Dopasuj zaznaczenie do ciemnego tła zegara** | Przy zatwierdzaniu próbuje dokładniej dopasować ramkę do wyświetlacza. Jeśli obcina cyfry, wyłącz i zaznacz zegar ponownie. |
| **Ręczne ustawienie obszaru** | Pozwala wpisać położenie i rozmiar zaznaczenia w pikselach. Nie musisz tego używać — wystarczy zaznaczenie myszą. |

Przy zmianie rozdzielczości źródła zaznaczenie skaluje się automatycznie. Jeśli zmieni się położenie zegara lub układ transmisji, zaznacz zegar ponownie.

## Gdy coś nie działa

| Problem | Co zrobić |
| --- | --- |
| Nie ma filtra na liście | Uruchom ponownie OBS, sprawdź folder instalacji i upewnij się, że dodajesz filtr do kamery lub źródła **Multimedia**. |
| Lista źródeł tekstowych jest pusta | Najpierw dodaj źródło **Tekst (GDI+)** lub **Tekst (FreeType 2)**. Następnie zamknij i otwórz ustawienia filtra. |
| Pojawia się komunikat o braku obrazu | Włącz źródło; dla filmu uruchom odtwarzanie. Sprawdź, czy używasz obsługiwanego rodzaju źródła. |
| Wtyczka odrzuca zaznaczenie | Zaznacz całe minuty i sekundy, np. `1:23`, razem z dwukropkiem. Nie zaznaczaj samych dziesiątych części sekundy. |
| Czas jest błędny lub się nie pojawia | Zaznacz sam zegar bez innych cyfr. Sprawdź ostrość i kontrast obrazu. Wyłącz automatyczne dopasowanie, jeśli obcina fragment zegara. |
| Odczyt w ustawieniach jest poprawny, ale tekst się nie zmienia | Sprawdź wybrane źródło tekstu oraz wyłącz czytanie tekstu z pliku. Po zmianie nazwy źródła wybierz je ponownie w filtrze. |
| Czas stoi po zniknięciu zegara | To oczekiwane zachowanie: wtyczka zachowuje ostatni odczyt i czeka na czytelny zegar. Nie odlicza czasu samodzielnie. |
| Pojawia się komunikat o formacie obrazu | W ustawieniach urządzenia spróbuj formatu **NV12**, **I420** lub **BGRA**, jeśli urządzenie pozwala go wybrać. |

Opis stanu na dole ustawień odświeża się po ponownym otwarciu ustawień filtra. Pokazuje ostatni odczyt, jego pewność, zatrzymanie zegara lub brak nowych klatek.

---

<details>
<summary><strong>Dla osób budujących wtyczkę ze źródeł</strong></summary>

### Windows

Wymagane: Visual Studio 2022 z narzędziami do tworzenia aplikacji w C++, CMake 3.28+ oraz dostęp do internetu. Konfiguracja pobiera zależności OBS i Qt.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix release\RelWithDebInfo
```

Skopiuj folder `release\RelWithDebInfo\seg7-clock-reader` do `%APPDATA%\obs-studio\plugins`, zgodnie z instrukcją instalacji powyżej.

### macOS

Wymagane: macOS 13+, Xcode 16 lub nowszy, CMake 3.28+ i dostęp do internetu. Konfiguracja CMake pobiera zależności:

```bash
cmake --preset macos
cmake --build --preset macos --config RelWithDebInfo
cmake --install build_macos --config RelWithDebInfo --prefix release/macos
```

Znajdź utworzony pakiet `seg7-clock-reader.plugin` w folderze `release/macos` i skopiuj go do `~/Library/Application Support/obs-studio/plugins/`. Następnie uruchom ponownie OBS.

Lokalny `Makefile` jest alternatywą dla osób mających już przygotowane zależności w `.deps/` i OBS w `/Applications/OBS.app`. Wymaga nagłówków OBS 32.2.2, Qt i SIMDe oraz pliku `obsconfig.h`. **Makefile nie pobiera tych plików automatycznie.** Polecenie `make` buduje pakiet dla Apple Silicon, a `make install` kopiuje go do folderu wtyczek użytkownika.

### Testy

Testy dekodera i śledzenia czasu nie wymagają OBS ani Qt:

```bash
cmake -S tests -B build-tests -DENABLE_SANITIZERS=ON
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```

Na macOS można też użyć `make test`. Polecenie `make test-obs` dodatkowo sprawdza konwersję obrazu i funkcje wywoływane przez OBS; wymaga tych samych zależności co lokalna kompilacja wtyczki. W konfiguracji testowej CMake odpowiada mu opcja `-DTEST_OBS_INTEGRATION=ON` z dostępnymi pakietami programistycznymi OBS i Qt.

Testy obejmują syntetyczne obrazy zegarów prostych i pochylonych, wszystkie sekundy od `00` do `59`, dziesiąte części sekundy, nieprawidłowe zaznaczenia, zatrzymanie zegara i odzyskiwanie odczytu po zmianie obrazu. Testy integracyjne sprawdzają m.in. odwrócone klatki, formaty pikseli, listę źródeł tekstowych i pomijanie zbędnych aktualizacji tekstu. Nie zastępują sprawdzenia własnego nagrania w OBS.

Automatyczne testy rdzenia uruchamiają się na Windows, macOS i Linux. Publikowanie znacznika wersji uruchamia budowę wydania Windows.

### Pliki projektu

- `src/clock_core.cpp` — odczyt cyfr i śledzenie czasu; przenośny C++17.
- `src/plugin-main.cpp` — integracja z OBS i ustawienia filtra.
- `src/roi-picker.cpp` — okno zaznaczania zegara.
- `tests/` — testy regresji dekodera, śledzenia czasu i integracji z OBS.
- `cmake/`, `CMakePresets.json`, `Makefile` — konfiguracja budowania.
- `.github/workflows/` — automatyczne testy, budowanie i wydania.

</details>

## Licencja

GPL-2.0. Pełna treść licencji znajduje się w pliku [LICENSE](LICENSE).
