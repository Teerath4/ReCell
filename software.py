"""
ReCell — Main Application (QRB2210 side)
Receives per-cell feature vectors from the STM32 sketch via Bridge, runs
each through the trained AI model, and pushes results to the WebUI dashboard.

Uses a locally-vendored copy of Edge Impulse's runner.py (ei_runner.py) to
bypass a packaging bug in the official edge_impulse_linux SDK, which pulls
in an unresolvable opencv dependency even for this non-camera use case.
"""

from arduino.app_utils import *
from arduino.app_bricks.web_ui import WebUI
from ei_runner import ImpulseRunner
import os

MODEL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modelfile.eim')
print("Loading model...")
runner = ImpulseRunner(MODEL_PATH)
runner.init()
print("Model loaded successfully.")

ui = WebUI()

# Holds each cell's result until all 4 have reported for the current pulse test.
cell_results = {}


def get_recommendation(soh):
    if soh >= 0.8:
        return "EV reuse"
    elif soh >= 0.6:
        return "Stationary storage"
    elif soh >= 0.4:
        return "UPS backup"
    else:
        return "Retire"


def run_inference_cell(cell_id, resistance_proxy, v_start, v_end, v_sag, mean_temp, discharge_duration_s):
    """Called from the sketch once per cell, after each automatic pulse test."""
    features = [resistance_proxy, v_start, v_end, v_sag, mean_temp, discharge_duration_s]
    result = runner.classify(features)

    # Clamp to a physically valid range — a model output outside [0, 1] means
    # the input features were outside anything seen during training, and
    # should never be displayed as a raw, uncapped percentage.
    soh = max(0.0, min(1.0, result['result']['classification']['value']))

    cell_results[cell_id] = {
        'soh_percent': round(soh * 100, 1),
        'recommendation': get_recommendation(soh)
    }
    print(f"Cell {cell_id}: SoH = {soh:.4f}")

    ui.send_message('cell_update', {
        'cell_id': cell_id,
        'soh_percent': cell_results[cell_id]['soh_percent'],
        'recommendation': cell_results[cell_id]['recommendation']
    })

    # Once all 4 cells have reported for this pulse test, compute the pack summary.
    if len(cell_results) == 4:
        pack_summary()
        cell_results.clear()

    return soh


def pack_summary():
    """Pack-level result: weakest cell determines the pack's usable capacity
    and recommendation, since a series pack can't outperform its worst cell."""
    weakest_cell_id = min(cell_results, key=lambda cid: cell_results[cid]['soh_percent'])
    weakest = cell_results[weakest_cell_id]

    print(f"Pack summary — weakest: Cell {weakest_cell_id} at {weakest['soh_percent']}%")

    ui.send_message('pack_update', {
        'pack_soh_percent': weakest['soh_percent'],
        'weakest_cell_id': weakest_cell_id,
        'recommendation': weakest['recommendation'],
    })


Bridge.provide("run_inference_cell", run_inference_cell)
print("Bridge ready, waiting for pulse test results...")

App.run()
