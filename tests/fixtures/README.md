# Próbki zegara do testów

Pliki PGM zawierają małe wycinki obrazu (113 × 33 piksele) z transmisji meczu użytej do odtworzenia błędnego odczytu. Zapisano jasność pikseli bez wygładzania i bez usuwania ramki. Wycinki zachowują ślady kompresji, pochylenie cyfr i przerwy w świecących segmentach.

Nazwy `clock-M-SS.pgm` podają ręcznie sprawdzony czas. `no-clock.pgm` przedstawia fragment obrazu bez zegara, a `blank.pgm` — jednolite tło. Te dwa pliki nie powinny dawać poprawnego odczytu.

Testy sprawdzają oryginalne wycinki, zaznaczenie z niewielkim marginesem i zaznaczenie bez górnej i dolnej ramki. Test integracji potwierdza też, że czas `1:27` trafia do źródła tekstowego dopiero po potwierdzeniu kilku klatek, a brak zegara zachowuje ostatni wynik z odpowiednim komunikatem.
