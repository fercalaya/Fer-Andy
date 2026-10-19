"""
graficar.py · Práctica de ADC
Envía un comando 'stream' a la Pico por el puerto serial, recibe las muestras,
las guarda en un CSV y grafica las muestras y su histograma.

Uso:
    python graficar.py <puerto> "<comando>" [--nombre NOMBRE] [--fs HZ]

Ejemplos:
    python graficar.py COM5 "stream 0 1000" --nombre ruido_ps_off --fs 1000
    python graficar.py /dev/ttyACM0 "stream 1 200" --nombre alias_900 --fs 900

El puerto es el del Pico puente. La Pico debe responder con una línea por muestra:
    índice código
Cualquier otra línea (eco, "ok", el prompt) se ignora.

Requisitos: pip install pyserial matplotlib
"""
import argparse
import csv
import sys
import time

import matplotlib.pyplot as plt
import serial


def leer_muestras(puerto, comando, n_esperadas, timeout_s):
    """Envía el comando y lee líneas 'índice código' hasta tener n muestras."""
    muestras = []
    inicio = time.time()
    puerto.reset_input_buffer()
    puerto.write((comando + "\r\n").encode())

    while len(muestras) < n_esperadas:
        if time.time() - inicio > timeout_s:
            print(f"Tiempo agotado: llegaron {len(muestras)} de {n_esperadas} muestras.")
            break
        linea = puerto.readline().decode(errors="ignore").strip()
        partes = linea.split()
        if len(partes) == 2 and partes[0].isdigit() and partes[1].isdigit():
            muestras.append((int(partes[0]), int(partes[1])))
    return muestras


def guardar_csv(nombre, muestras):
    with open(nombre + ".csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["indice", "codigo"])
        w.writerows(muestras)
    print(f"Guardado: {nombre}.csv")


def resumen(codigos):
    minimo, maximo = min(codigos), max(codigos)
    promedio = sum(codigos) / len(codigos)
    print(f"Muestras: {len(codigos)}   mínimo: {minimo}   máximo: {maximo}   "
          f"rango: {maximo - minimo}   promedio: {promedio:.1f}")


def graficar(nombre, muestras, fs):
    indices = [m[0] for m in muestras]
    codigos = [m[1] for m in muestras]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5),
                                   gridspec_kw={"width_ratios": [3, 1]})

    if fs:
        x = [i * 1000.0 / fs for i in indices]
        ax1.set_xlabel("Tiempo (ms)")
    else:
        x = indices
        ax1.set_xlabel("Muestra")
    ax1.plot(x, codigos, marker=".", markersize=3, linewidth=1)
    ax1.set_ylabel("Código")
    ax1.set_title(nombre)
    ax1.grid(alpha=0.3)

    # Histograma: una barra por código si el rango es pequeño (ruido),
    # o 50 barras si la señal recorre mucho rango (aliasing, filtros)
    minimo, maximo = min(codigos), max(codigos)
    if maximo - minimo <= 200:
        ax2.hist(codigos, bins=range(minimo, maximo + 2), orientation="horizontal",
                 align="left", rwidth=0.85)
    else:
        ax2.hist(codigos, bins=50, orientation="horizontal", rwidth=0.85)
    ax2.set_xlabel("Veces")
    ax2.set_title("Histograma")
    ax2.grid(alpha=0.3)

    fig.tight_layout()
    fig.savefig(nombre + ".png", dpi=150)
    print(f"Guardado: {nombre}.png")
    plt.show()


def main():
    p = argparse.ArgumentParser(description="Captura y grafica un stream del ADC.")
    p.add_argument("puerto", help="Puerto serial del Pico puente (COM5, /dev/ttyACM0...)")
    p.add_argument("comando", help='Comando a enviar, por ejemplo "stream 0 1000"')
    p.add_argument("--nombre", default="captura", help="Nombre de los archivos de salida")
    p.add_argument("--fs", type=float, default=None,
                   help="Frecuencia de muestreo usada, para graficar en milisegundos")
    p.add_argument("--timeout", type=float, default=40.0, help="Segundos máximos de espera")
    args = p.parse_args()

    partes = args.comando.split()
    if len(partes) != 3 or partes[0] != "stream":
        sys.exit('El comando debe tener la forma: "stream <canal> <n>"')
    n = int(partes[2])

    with serial.Serial(args.puerto, 115200, timeout=1) as puerto:
        time.sleep(0.5)
        muestras = leer_muestras(puerto, args.comando, n, args.timeout)

    if not muestras:
        sys.exit("No llegaron muestras. Revisar el puerto, el cableado y el comando.")

    guardar_csv(args.nombre, muestras)
    resumen([m[1] for m in muestras])
    graficar(args.nombre, muestras, args.fs)


if __name__ == "__main__":
    main()
