import subprocess
import pandas as pd
import matplotlib.pyplot as plt
import io
import os

def run_benchmark():
    print("Compiling benchmark_mapper.cpp...")
    subprocess.run(["g++", "-O3", "-std=c++17", "benchmark_mapper.cpp", "-o", "benchmark_mapper"], check=True)
    
    print("Running benchmark...")
    result = subprocess.run(["./benchmark_mapper"], capture_output=True, text=True, check=True)
    return result.stdout

def plot_data(csv_data):
    df = pd.read_csv(io.StringIO(csv_data))
    
    # Configure plot for academic paper styling
    plt.style.use('seaborn-v0_8-whitegrid')
    plt.rcParams.update({
        "font.family": "serif",
        "font.size": 12,
        "axes.labelsize": 14,
        "axes.titlesize": 16,
        "legend.fontsize": 12,
        "xtick.labelsize": 11,
        "ytick.labelsize": 11,
        "lines.linewidth": 2,
        "lines.markersize": 6
    })

    fig, ax = plt.subplots(figsize=(8, 5))

    # Plot metrics
    ax.plot(df['size'], df['hit'], marker='o', linestyle='-', color='#1f77b4', label=r'IPswen $\rightarrow$ vIPv4 (Hit)')
    ax.plot(df['size'], df['rev'], marker='s', linestyle='-', color='#2ca02c', label=r'vIPv4 $\rightarrow$ IPswen (Reverse)')
    
    # Miss has much higher latency, we plot it on the same graph but with its own distinctive line
    ax.plot(df['size'], df['miss'], marker='^', linestyle='--', color='#d62728', label=r'Allocate New vIPv4 (Miss)')

    ax.set_xscale('log') # Use logarithmic scale to show tree scaling properties clearly
    ax.set_xlabel('Active Mappings (Count, Log Scale)')
    ax.set_ylabel('Latency (ns)')
    ax.set_title('Address Translation Map Performance vs Capacity')

    ax.grid(True, which="both", ls="-", alpha=0.5)
    ax.legend(loc='upper left')

    plt.tight_layout()

    # Save outputs
    png_path = 'DnsMapper_Performance.png'
    pdf_path = 'DnsMapper_Performance.pdf'
    plt.savefig(png_path, dpi=300, bbox_inches='tight')
    plt.savefig(pdf_path, format='pdf', bbox_inches='tight')
    
    print(f"Plots saved to {png_path} and {pdf_path}")

if __name__ == '__main__':
    try:
        csv_output = run_benchmark()
        plot_data(csv_output)
    except Exception as e:
        print(f"Error: {e}")
