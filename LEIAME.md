# SoR1-X360

*[English](README.md) | Português*

O Streets of Rage (1991) rodando **nativo** no Xbox 360 desbloqueado: 60 fps, 1280x720 com a visão mais larga em 16:9, filtros de imagem, troca de botões, save state e o menu de configurações.

Não é um emulador rodando o jogo instrução por instrução. Ele parte do [Streets-Of-Rage-Recomp](https://github.com/OriginalSwayze/Streets-Of-Rage-Recomp), do OriginalSwayze, que traduz o código 68000 do próprio jogo para C, e eu portei para o Xbox 360: vídeo na GPU do console (D3D9), XInput, XAudio2, arquivos e menus. A tradução é feita no seu PC a partir da **sua** ROM e compilada dentro do `.xex`. Os chips de som e de vídeo continuam emulados em volta.

> **Aqui não tem jogo nenhum.** Nem ROM, nem o código traduzido, nem `.xex` pronto, nem o SDK da Microsoft. Você precisa da sua ROM e das ferramentas abaixo para compilar. Não me peça ROM.

---

## O que funciona

- 60 fps, um quadro do jogo por atualização da TV
- 1280x720, 16:9 com a área de jogo ampliada (ou o 4:3 original)
- Filtros: scanlines, CRT, LCD (na GPU)
- Troca de botões, vibração, dois controles
- Save state (9 slots) e configurações salvas no `settings.ini`
- Som num núcleo próprio

Joguei no console de verdade as primeiras fases, com save state, a 60 fps.

## O que você precisa

### No console
- Xbox 360 desbloqueado (RGH/JTAG) com Aurora, FreeStyle Dash ou XeXMenu

### No PC (Windows 10/11)
1. **WSL** com Ubuntu. A compilação do Xbox roda nele.
2. **[OXDK](https://github.com/MrMilenko/OXDK)** no WSL, com o LLVM dele compilado com os patches do Xenon que vêm em `OXDK/xbox360/llvm`, mais o `port/tools/patches/llvm-lld-thunk-r0.patch` (thunk de desvio longo do lld que não usa o r12).
3. **Arquivos do SDK do Xbox 360 (XDK)** onde o OXDK procura (`~/xdk360/XDK`).
4. **Visual Studio 2022 Build Tools** com C++ e o clang, para compilar a base no PC.
5. **Node.js**, para o tradutor da base.
6. **Git**.

### A ROM
**Streets of Rage (World)**, SHA-1 `3d74dbc81f3472a5bde45bf265e636a72a314667`.

Tem que ser a primeira versão. A **Rev A** (série terminando em -01) não funciona: o código é traduzido por endereço, e na Rev A ele está em outros lugares.

## Compilando

**1. Baixe este projeto e a base**, numa pasta sem espaço no caminho:

```bash
git clone https://github.com/EduDicaseGameplay/SoR1-X360.git
cd SoR1-X360
git clone --recursive https://github.com/OriginalSwayze/Streets-Of-Rage-Recomp.git referencias/Streets-Of-Rage-Recomp
git -C referencias/Streets-Of-Rage-Recomp checkout 006ff0f
```

O port foi feito em cima desse commit da base.

**2. Traduza o jogo.** Coloque sua ROM em `rom\` e crie o `referencias\Streets-Of-Rage-Recomp\local.json` com o caminho dela:

```json
{ "roms": { "sor1": "C:\\caminho\\para\\SoR1-X360\\rom\\sua_rom.md" } }
```

Depois, nessa pasta:

```
set GAME=sor1
node tools\recomp.mjs
```

**3. Compile a base para PC** uma vez. A compilação do Xbox usa a configuração, as tabelas do Musashi e as fontes que ela gera:

```
port\tools\pcbuild.bat
```

**4. Compile o `.xex`** no WSL:

```bash
bash port/tools/build.sh
```

O `make` não aceita espaço no caminho. Se o projeto estiver em `C:\Users\...\Desktop\Minha pasta`, crie um link antes: `ln -s "/mnt/c/.../SoR1-X360" ~/sor1` e rode `bash ~/sor1/port/tools/build.sh`.

O resultado sai em `port/build/xex/default.xex`.

## No console

Crie uma pasta (por exemplo `Hdd1:\Games\SoR1X360`) com:

```
default.xex
sor1.bin       sua ROM (.bin, .md, .gen ou .smd)
```

Controles: direcional ou analógico esquerdo move, A ataque, B pulo, X: especial (o carro da polícia), Start pausa. **Back** abre o menu (LB/RB trocam as abas): Controls, save state, Widescreen, Filter, som. Back + Start segurados por 3 segundos saem do jogo.

Se algo der errado, o `sor1_log.txt` ao lado do `.xex` mostra onde parou.

## Créditos

- [Streets-Of-Rage-Recomp](https://github.com/OriginalSwayze/Streets-Of-Rage-Recomp), do OriginalSwayze (MIT): o tradutor e o runtime que este port modifica
- [OXDK](https://github.com/MrMilenko/OXDK), do MrMilenko (domínio público): as ferramentas de compilação
- Musashi, o z80 do superzazu e o ymfm do Aaron Giles, que vêm com a base

Veja `THIRD_PARTY.txt` e `LICENSE`. Streets of Rage pertence à SEGA.
