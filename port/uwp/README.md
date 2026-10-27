# khdays-port para UWP (Xbox One, Dev Mode)

Este diretório junta o que o port precisa para rodar como aplicativo UWP no
Xbox One (Dev Mode). O build continua sendo feito no **PC, com Visual Studio** —
não há como compilar UWP fora do Windows (nem no Termux/Android).

## O que mudou no código para UWP

- `port/CMakeLists.txt`: novo modo `KHDAYS_UWP=ON` (junto com
  `-DCMAKE_SYSTEM_NAME=WindowsStore`): CRT estático (o pacote não precisa do
  VCLibs), SDL3-UWP pronta em vez de baixada, sem `dbghelp`/`winmm`/`winhttp`,
  BCrypt no lugar de RtlGenRandom, sem `update_install_win.c` nem `https_win.c`,
  `diag.c` trocado por `runtime/core/diag_uwp.c`, e o entry `uwp/main_uwp.cpp`.
- `port/uwp/main_uwp.cpp`: o entry UWP. Inclui `SDL3/SDL_main.h` (o fork SDL3-UWP
  faz o plumbıng do Windows Runtime) e chama `khdays_main` (o `main` de
  `start.c`, renomeado quando `KHDAYS_UWP` está definido). **Compilar com /ZW**.
- `port/runtime/core/memory.c`: sem as APIs de placeholder
  (`VirtualAlloc2`/`MapViewOfFile3`), a janela de memória do jogo vira uma
  reserva comum, as regiões são commitadas no lugar e os espelhos são mapeados
  com `MapViewOfFileEx`. Se o sistema recusar, o jogo avisa e para.
- `port/host/net/https_uwp.cpp`: os pedidos HTTPS com IXMLHTTPRequest2 (a UWP
  não tem WinHTTP).
- `port/host/net/net_win.c`: `BCryptGenRandom` no lugar de RtlGenRandom;
  `GetBestRoute` (chute do gateway do multiplayer) fica desligado — o endereço
  do anfitrião da sala deve ser dado à mão.
- `port/host/app/instance_win.c`: a enumeração de processos (outras cópias
  rodando) fica desligada; na UWP cada instância está sozinha.
- `port/runtime/core/start.c`: `main` vira `khdays_main`; sem console; a pasta
  de trabalho é o **LocalState** do aplicativo (`SDL_GetPrefPath`), onde o jogo
  grava saves, mods, configurações e logs; o `data/extracted` do pacote é
  copiado para lá na primeira execução.
- Sem o instalador e sem o atualizador: na UWP a atualização é reinstalar o
  pacote (a checagem de versão nova continua, só o aviso).

## O que você precisa

- **PC Windows** com Visual Studio (carga "Desenvolvimento de aplicativos
  WinUI/UWP" + "Desenvolvimento para desktop com C++"), Git, CMake, Python 3.11+.
- **Xbox One em Dev Mode** (ativação gratuita em "Configurações do sistema →
  Dev Mode"; consulte também o [Dev Mode Wiki](https://github.com/XboxEmulationHub/devmodewiki)).
- Sua **cópia legal da ROM europeia** de KINGDOM HEARTS 358/2 Days.
- O repositório `Yokimitsuro/khdays-decomp` (o ZIP do port traz só o ponteiro):
  `git submodule update --init --recursive` dentro da pasta do port.

## 1. A SDL3-UWP

O build usa o fork [SternXD/SDL3-uwp](https://github.com/SternXD/SDL3-uwp),
branch **`uwp-3.4.8`** (a SDL oficial não suporta UWP):

```powershell
git clone -b uwp-3.4.8 https://github.com/SternXD/SDL3-uwp.git
cd SDL3-uwp
```

Duas maneiras (o [guia da cena](https://github.com/XboxEmulationHub/devmodewiki)
detalha as duas):

**Visual Studio:** abra `VisualC-WinRT\SDL-UWP.sln`, configuração Release,
plataforma x64, e compile o projeto SDL UWP. A saída fica em `VisualC-WinRT`
(includes do `include` do clone, `SDL3.lib` e `SDL3.dll` na pasta de saída).

**CMake + Ninja** (em um prompt de ferramentas do VS):

```cmd
set INSTALLDIR=C:\sdl3-uwp-install
cmake -B build-uwp -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 ^
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX="%INSTALLDIR%" -DBUILD_SHARED_LIBS=ON -DSDL_SHARED=ON ^
  -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -G Ninja
cmake --build build-uwp --config Release --parallel
cmake --install build-uwp --config Release
```

Anote onde ficaram o `include`, o `SDL3.lib` e a `SDL3.dll`.

## 2. Os dados do jogo

No PC, como o README do port descreve:

```powershell
py .\tools\verify_rom.py "E:\caminho\game.nds"
py .\tools\extract_data.py "E:\caminho\game.nds"
```

Os dados vão para `data\extracted\<hash>\`.

## 3. Preparar e compilar o port

O `prepare.py` roda primeiro (igual ao `build.py` faz; ele gera os fontes da
construção em `build\port\gen-x64`):

```powershell
py .\port\tools\prepare.py
```

Configure e compile com o modo UWP (gerador do Visual Studio, x64):

```cmd
cmake -B build\port\obj-uwp -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 ^
  -DKHDAYS_UWP=ON ^
  -DKHDAYS_SDL3_PREFIX=C:\sdl3-uwp-install ^
  -DKHDAYS_GEN=%CD%\build\port\gen-x64
cmake --build build\port\obj-uwp --config Release --target khdays-native
```

(Ajuste o gerador ao seu Visual Studio e o `KHDAYS_SDL3_PREFIX` à pasta da
etapa 1. O executável sai em `build\port\obj-uwp\Release\khdays-native.exe`.)

## 4. Empacotar

```powershell
powershell -ExecutionPolicy Bypass -File port\uwp\make_package.ps1 `
  -BuildDir build\port\obj-uwp\Release `
  -Sdl3Dll C:\sdl3-uwp-install\bin\SDL3.dll `
  -DataDir data\extracted\<hash>
```

O script monta o layout (`Package.appxmanifest`, `Assets`, o executável, a
`SDL3.dll` e, se dado, o `data\extracted`), cria um certificado próprio na
primeira vez (`build\port\uwp\khdays-dev.pfx`, senha `khdays`), empacota com
`MakeAppx` e assina com `signtool`. O resultado:
`build\port\uwp\khdays-port.appx`.

## 5. O certificado

O console só instala pacotes assinados por um certificado em que confia. Com o
pacote assinado pelo certificado próprio do script, ponha o certificado no
console **antes** da primeira instalação: no Device Portal do Xbox, exporte o
`.cer` do certificado (no Windows, `certmgr.msc` → Pessoal → Certificados →
"khdays-port dev" → Exportar, sem a chave privada) e instale-o pelo Device
Portal (página **Apps**; se o seu build do portal não aceitar certificado, o
[Dev Mode Wiki](https://github.com/XboxEmulationHub/devmodewiki) tem o passo a
passo atualizado da cena).

## 6. Instalar no Xbox

1. No console, em Dev Mode, abra o **Device Portal** (o IP aparece nas
   configurações do Dev Mode; do PC, `https://<IP do console>`).
2. Página **Apps** → **Install app** → envie `khdays-port.appx`.
3. Se você não empacotou o `data\extracted` (-DataDir), copie-o depois para o
   **LocalState** do aplicativo pelo explorador de arquivos do Device Portal
   (pasta `LocalState`, com `data\extracted\...` dentro). A primeira execução
   usa o que estiver lá.
4. O jogo aparece na lista de apps do Dev Mode. Saves, mods, configurações e
   logs ficam no LocalState do app (`khdays\port`).

## O que testar primeiro (em ordem de risco)

1. **SDL_GPU no fork:** se o `khdays-native` abrir e o jogo desenhar, a parte
   mais incerta passou. Se o dispositivo gráfico não abrir, veja o log no
   Device Portal — o próximo passo é um patch no fork para vincular o D3D12
   estaticamente.
2. **A memória:** o log diz se as APIs de placeholder funcionaram ou se a
   reserva simples foi usada (e se `MapViewOfFileEx` conseguiu mapear os
   espelhos).
3. **Vídeos (Media Foundation)** e **multiplayer** (UDP no console).
4. **Desempenho:** a GPU do Xbox One é pequena — comece com a resolução do jogo
   no próprio menu de configurações do port, e suba devagar.

## Limitações conhecidas desta adaptação

- Sem instalador/atualizador (na UWP, reinstale o pacote para atualizar).
- Multiplayer: o chute de gateway fica desligado; o endereço do anfitrião da
  sala deve ser dado à mão. O servidor (`khdays-server`) continua sendo um
  programa de PC.
- O diagnóstico de travamentos sem nomes de função (`diag_uwp.c`), e a
  ferramenta de script `KHDAYS_INPUT` fica sem os nomes de função.
- A troca de contextos (`context_win.c` + `context_x64.asm`) continua a
  original: escrita em campos do TEB não documentados — se o jogo travar de
  forma estranha, este é o próximo suspeito.
