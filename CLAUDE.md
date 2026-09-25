As convenções deste repositório estão em [AGENTS.md](AGENTS.md) — leia antes de
começar. O contexto técnico e o estado atual do reparo estão em
[README.md](README.md).

Três coisas que não podem ser esquecidas:

1. **Repositório privado.** Material pessoal de família. Existe **um** remoto,
   privado, criado pelo usuário em 2026-09-18:
   `git@github.com:ChrisCamilo/fix-marriage.git`. Nunca criar outro remoto,
   nunca torná-lo público, nunca publicar nada em outro lugar. **O push é sempre
   do usuário**: o agente faz os commits e nunca roda `git push`.
2. **O MP4 nunca é modificado.** O `patches.txt` **não** é append-only, mas
   **toda mudança nele passa pelo usuário antes** — é fonte de verdade, e linha
   que entra afirma "este bit estava corrompido". As duas juntas são a base;
   todo o resto é derivado.
3. **Manter o `README.md` atualizado** com números medidos depois de cada
   corrida — separando o que foi consertado do que já estava intacto.
