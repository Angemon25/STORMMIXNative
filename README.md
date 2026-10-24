# STORM MIX Native

**Mixer audio digitale sperimentale per Raspberry Pi 4**, sviluppato in C++17, Qt 6/QML e ALSA. Interfaccia touchscreen 1024×600, controllo MIDI con Behringer BCF2000 e DSP software.

**Versione sorgente:** `0.6.39 Beta` · **Stato:** prototipo sperimentale, non validato per eventi live.

## Hardware previsto

- Raspberry Pi 4 con Raspberry Pi OS e display touch 1024×600.
- Behringer UMC202HD USB (2 ingressi / 2 uscite fisiche).
- Behringer BCF2000 via USB MIDI, modalità MIDI standard, preset BC Manager 1 «Reason Mixer 1».

L'interfaccia include 8 channel strip, 4 AUX e routing/uscite logiche; **non** significa che la UMC202HD fornisca 8 I/O fisici.

## Funzionalità

- Mixer con fader, pan, mute, solo, meter e selezione canale.
- EQ e dinamiche di canale, AUX, FX e parametri di uscita in DSP software (funzioni e qualità in evoluzione).
- BCF2000 con 18 banchi virtuali e feedback MIDI, con limitazioni documentate.
- Interfaccia touch e avvio fullscreen tramite `Window.FullScreen`.
- Impostazioni audio ALSA e test DSP/FFT CTest.

**Non implementati in questa release:** pagina DCA/GROUP, aggregazione sincronizzata di più schede audio, garanzie di latenza/stabilità live.

## Installazione Raspberry Pi OS

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
  qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtqml-workerscript \
  libasound2-dev alsa-utils
```

## Compilazione, test e avvio

Dalla cartella del progetto:

```bash
bash scripts/build-and-run.sh
```

In alternativa:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/storm-mix
```

Lo script usa Wayland quando è presente la sessione grafica prevista. **Eseguirlo come utente della sessione desktop, non con sudo.** Su sessioni diverse potrebbe essere necessario adattare `WAYLAND_DISPLAY`/`QT_QPA_PLATFORM`.

## Struttura del repository

| Percorso | Descrizione |
|---|---|
| `Main.qml` | Interfaccia grafica touchscreen |
| `main.cpp` | Avvio Qt |
| `mixercontroller.*` | Stato mixer, controlli e parametri |
| `audioengine.*` | Motore audio ALSA |
| `dsp.h` | DSP |
| `bcf2000.cpp` | Controllo e feedback MIDI |
| `scripts/` | Script compilazione/avvio |
| `docs/` | Architettura, BCF2000, problemi, roadmap, note versioni |

## Documentazione

- [Architettura e limiti](docs/ARCHITECTURE.md)
- [Mappatura BCF2000](docs/BCF2000.md)
- [Roadmap DCA e GROUP](docs/ROADMAP.md)
- [Risoluzione problemi](docs/TROUBLESHOOTING.md)
- [Cronologia tecnica](docs/history/)

## Sicurezza e stato dei test

Questo repository contiene **codice sperimentale**. La build Qt/ALSA e il comportamento reale su Raspberry Pi 4 non sono stati verificati durante la preparazione del pacchetto GitHub. Prima di collegare un impianto audio: volumi bassi, prove di mute, clipping, feedback, XRUN e recupero errori. Non impiegare in spettacoli o applicazioni critiche senza collaudo.
