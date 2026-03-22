# PROJETO: Scinterm NotCurses

## DESCRIÇÃO GERAL
Implementar um port do Scintilla (editor de texto) para o NotCurses (biblioteca de terminal moderna). O projeto deve fornecer um widget de editor de texto completo com suporte a true color, Unicode, syntax highlighting e auto-completion.

## OBJETIVO
Criar uma biblioteca C/C++ que integre o Scintilla com o NotCurses, permitindo que aplicações de terminal tenham um editor de texto profissional.

## REQUISITOS TÉCNICOS

scinterm/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .gitignore
├── .clang-format
├── .github/workflows/build.yml (para Fedora 43)
├── include/
│ └── scinterm_notcurses.h (API pública)
├── src/
│ └── plat/
│ ├── scinterm_plat.h (header da plataforma)
│ ├── scinterm_plat.c (implementação da plataforma)
│ ├── scinterm_notcurses.cpp (classe principal)
│ ├── scinterm_wcwidth.h (utilitários Unicode)
│ └── scinterm_wcwidth.c (implementação wcwidth)
├── examples/
│ ├── simple_editor.c
│ ├── multi_window.c
│ └── syntax_highlighting.c
├── tests/
│ └── test_editor.c
└── cmake/
└── scinterm_notcurses-config.cmake.in

text

### 2. FUNCIONALIDADES PRINCIPAIS

#### 2.1 Integração com NotCurses
- Inicialização do NotCurses
- Criação e gerenciamento de planes
- Renderização eficiente
- Suporte a true color (RGB 24-bit)

#### 2.2 Scintilla Core
- Integrar todas as funcionalidades do Scintilla
- Suporte a mensagens SCI_*
- Gerenciamento de documentos
- Syntax highlighting (opcional com lexers)

#### 2.3 Input Handling
- Teclado (mapeamento de teclas NotCurses → Scintilla)
- Mouse (cliques, drag, scroll)
- Foco de janela

#### 2.4 Unicode Support
- Largura de caracteres (wcwidth)
- UTF-8 handling
- Caracteres combinantes

#### 2.5 UI Elements
- Barras de rolagem
- Marcadores de linha
- Calltips
- Auto-completion listbox

### 3. API PÚBLICA (C)

```c
// Inicialização
bool scintilla_notcurses_init(void);
void scintilla_notcurses_shutdown(void);

// Gerenciamento de instância
void *scintilla_new(callback_t callback, void *userdata);
void scintilla_delete(void *sci);
struct ncplane *scintilla_get_plane(void *sci);

// Mensagens Scintilla
sptr_t scintilla_send_message(void *sci, unsigned int msg,
                              uptr_t wParam, sptr_t lParam);

// Input
void scintilla_send_key(void *sci, int key, int modifiers);
bool scintilla_send_mouse(void *sci, int event, int button,
                          int modifiers, int y, int x);
bool scintilla_process_input(void *sci, struct notcurses *nc);

// Renderização
void scintilla_render(void *sci);
void scintilla_resize(void *sci);
void scintilla_set_focus(void *sci, bool focus);
void scintilla_update_cursor(void *sci);

// Clipboard
char *scintilla_get_clipboard(void *sci, int *len);
4. CONFIGURAÇÕES DE BUILD
CMakeLists.txt deve incluir:
Detecção do NotCurses (pkg-config)

Opções: BUILD_SHARED_LIBS, BUILD_EXAMPLES, ENABLE_LEXERS

Suporte a Fedora 43, Ubuntu, macOS

Instalação de headers e bibliotecas

GitHub Actions (fedora:43):
Build em Fedora 43 container

Testes automatizados

Geração de RPM

Upload de artefatos

5. EXEMPLOS MÍNIMOS
simple_editor.c:
Inicialização

Loop principal

Processamento de teclas

Renderização

6. TESTES
Testes unitários para operações básicas

Testes de Unicode

Testes de seleção/cópia/cola

Testes de performance

7. DOCUMENTAÇÃO
README.md com instruções de build e uso

Comentários Doxygen em todos os headers

Exemplos de código

ENTREGÁVEIS ESPERADOS
✅ Código fonte completo e funcional

✅ Sistema de build CMake funcional

✅ GitHub Actions workflow para Fedora 43

✅ Documentação básica

✅ Exemplos de uso

✅ Testes automatizados

PRAZO E PRIORIDADES
Prioridade 1: Core funcionando (renderização, input)

Prioridade 2: Unicode e true color

Prioridade 3: Features avançadas (auto-complete, syntax highlighting)

Prioridade 4: Empacotamento e CI/CD

OBSERVAÇÕES IMPORTANTES
Usar C++17 para o código principal

Manter compatibilidade com C para a API pública

Suporte a Fedora 43 é obrigatório

Código deve ser bem comentado (inglês)

Seguir boas práticas de segurança e performance.


