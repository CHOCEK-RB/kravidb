# Spec: Refinamiento de Diseño — Superficies Mate, Márgenes Amplios y Micro-interacciones

## Objective
Pulir la interfaz de `kravidb web` eliminando el abuso de degradados genéricos tipo "sitio común de IA":
- Diseñar superficies mate sólidas y limpias (`#141124`, `#19152e`) con bordes nítidos de 1px y acentos pastel selectivos.
- Ampliar los márgenes internos de las losas físicas: mayor separación vertical entre tuplas (`GAP: 9px`), mayor separación vertical entre ranuras y un margen generoso (22px) entre las tuplas y la barra de llenado.
- Reemplazar el fondo de la cavidad libre con un patrón estático tipo matriz de puntos (*dot grid* de memoria).
- Micro-interacciones elásticas (*spring*) solo bajo demanda (hover/clic), con 0% de impacto en CPU en reposo.
- Estado vacío (*empty state*) amigable e ilustrado en la lente de bytes (`HexByteViewer`).

## Commands
```bash
Dev: bun run dev              # http://127.0.0.1:5173
Build: bun run build          # vite build -> dist/
Check All: bun run check:all  # format + linter + svelte-check
```

## Project Structure
```
web/
├── src/
│   ├── app.css                       # Superficies mate sólidas, reducción de degradados
│   ├── lib/components/
│   │   ├── storage/PageSlab.svelte      # Márgenes ampliados, separación de gauge y dot grid
│   │   ├── storage/HexByteViewer.svelte # Empty state ilustrado
│   │   └── workbench/CommandDeck.svelte # Botones y pills de alta fidelidad
```

## Code Style
- Superficies mate sólidas (`background: #141124`, `border: 1px solid var(--line-2)`).
- Micro-interacciones elásticas: `transition: transform 200ms var(--ease-spring), border-color 160ms ease;`.
- Separación generosa: mínimo 9px entre bloques de datos y 20px entre tuplas y barra de capacidad.

## Boundaries
- **Siempre:** Mantener legibilidad rigurosa de offsets y tamaños en `JetBrains Mono`.
- **Nunca:** Reintroducir bucles de luz continuos o degradados estridentes que recarguen la CPU.
- **Nunca:** Tocar los archivos del mock (`src/lib/api/types.ts`, `src/lib/api/mock.ts`).

## Success Criteria
- [ ] La separación vertical entre tuplas aumenta a 9px.
- [ ] La separación entre los bloques de tuplas y la barra de llenado vertical es de al menos 20px.
- [ ] Las ranuras tienen un espacio intermedio visible (4px de margen).
- [ ] La cavidad libre muestra una cuadrícula de memoria tipo *dot grid* estática y limpia.
- [ ] Los degradados genéricos de fondo y tarjetas se sustituyen por superficies mate elegantes.
- [ ] `HexByteViewer` cuenta con un empty state ilustrado.
- [ ] `bun run check:all` y `bun run build` aprueban con 0 errores y 0 warnings.
