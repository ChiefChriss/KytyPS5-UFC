# test-image-cpu-readback-fault

Rama aparcada a propósito. **No integrar en `main` tal cual.**

## Qué es

Alternativa a `graphics: write small GPU-written tiled images back to guest memory` (el arreglo de la moto de
agua de GTA V que sí está en `main`, que copia al guest en cada frame las imágenes *tiled* escritas por la GPU de
hasta 64x64 texels).

Aquí, con `--image-cpu-readback fault`, las páginas de las imágenes *tiled* escritas por la GPU (hasta
`--image-readback-max`, 4 MiB por defecto) se protegen contra lectura; cuando la CPU las lee, el fallo descarga la
imagen, la detila a la memoria del guest y desprotege. Solo se copia lo que la CPU lee y con el dato actual (sin la
carrera fence -> publicación asíncrona del modo por frame). Por defecto la rama sigue en `small`.

Commit: `graphics: read GPU-written tiled images back when the CPU reads them`.

## Por qué se dejó aquí

En `fault`, en GTA V las boyas, rampas de salto y otros objetos que flotan en el mar **vibran cuando el personaje
se mueve**. En `small` no pasa.

Hipótesis (no verificada): la física del agua combina el origen de la rejilla de olas (imagen *tiled* 1x1
R32G32_SFLOAT, que `fault` sirve con el dato del frame actual) con los mapas de alturas de 256x256, que son
imágenes *lineales* y siguen copiándose por frame con retraso. Origen actual + alturas de un frame anterior =
alturas desplazadas cuando la rejilla se mueve con el personaje. En `small` ambos llegan con el mismo retraso y son
coherentes.

## Si se retoma

- Cubrir también las imágenes lineales con el modo `fault` (origen y alturas del mismo frame; de paso arreglaría la
  carrera de la deformación de coches, commit 9cab6a32), validando coches y moto de agua.
- Diario de la implementación y cifras: `_Tools/image_cpu_readback_log.md`; plan: `_Tools/image_cpu_readback_plan.md`
  (copias locales del usuario, no versionadas).
