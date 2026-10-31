#!/usr/bin/env python3
"""
Magnetometer Calibration Tool

A pure Python implementation that reads data from a serial port and provides real-time
visualization and calibration similar to MotionCal.

Usage:
    python magnetometer_calibration.py
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.animation import FuncAnimation
from mpl_toolkits.mplot3d import Axes3D
import scipy.optimize as optimize
import serial
import threading
import time
import json
from collections import deque
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import os
import sys


class MagnetometerCalibrator:
    """Main class for magnetometer calibration and visualization"""
    
    def __init__(self, root):
        self.root = root
        self.root.title("Magnetometer Calibration Tool")
        self.root.geometry("1000x700")
        
        # Magnetometer data
        self.data_buffer = deque(maxlen=5000)
        self.is_collecting = False
        self.collection_thread = None
        self.serial_port = None
        
        # Calibration parameters
        self.offset_x = 0.0
        self.offset_y = 0.0
        self.offset_z = 0.0
        self.W = np.eye(3)  # Soft iron transformation matrix
        self.is_calibrated = False
        self.fit_quality = 0.0
        self.field_reference = 1.0
        
        # Create UI
        self.create_ui()
        
        # Create figure for plotting
        self.create_figure()
        
        # Start animation
        self.anim = FuncAnimation(self.fig, self.update_plot, interval=50, blit=True)
        
    def create_ui(self):
        """Create the user interface"""
        # Create main frame
        main_frame = ttk.Frame(self.root, padding=10)
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # Create top control panel
        control_frame = ttk.LabelFrame(main_frame, text="Serial Connection", padding=10)
        control_frame.pack(fill=tk.X, pady=5)
        
        # Serial port settings
        ttk.Label(control_frame, text="Port:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.port_var = tk.StringVar(value="COM3")
        self.port_entry = ttk.Entry(control_frame, textvariable=self.port_var, width=10)
        self.port_entry.grid(row=0, column=1, sticky=tk.W, padx=5)
        
        ttk.Label(control_frame, text="Baud:").grid(row=0, column=2, sticky=tk.W, padx=5)
        self.baud_var = tk.IntVar(value=115200)
        self.baud_combo = ttk.Combobox(control_frame, textvariable=self.baud_var, width=8)
        self.baud_combo['values'] = (9600, 19200, 38400, 57600, 115200, 230400)
        self.baud_combo.grid(row=0, column=3, sticky=tk.W, padx=5)
        
        ttk.Label(control_frame, text="Format:").grid(row=0, column=4, sticky=tk.W, padx=5)
        self.format_var = tk.StringVar(value="json")
        self.format_combo = ttk.Combobox(control_frame, textvariable=self.format_var, width=6)
        self.format_combo['values'] = ("json", "csv")
        self.format_combo.grid(row=0, column=5, sticky=tk.W, padx=5)
        
        # Keys for data
        ttk.Label(control_frame, text="X key:").grid(row=1, column=0, sticky=tk.W, padx=5)
        self.x_key_var = tk.StringVar(value="mx")
        self.x_key_entry = ttk.Entry(control_frame, textvariable=self.x_key_var, width=6)
        self.x_key_entry.grid(row=1, column=1, sticky=tk.W, padx=5)
        
        ttk.Label(control_frame, text="Y key:").grid(row=1, column=2, sticky=tk.W, padx=5)
        self.y_key_var = tk.StringVar(value="my")
        self.y_key_entry = ttk.Entry(control_frame, textvariable=self.y_key_var, width=6)
        self.y_key_entry.grid(row=1, column=3, sticky=tk.W, padx=5)
        
        ttk.Label(control_frame, text="Z key:").grid(row=1, column=4, sticky=tk.W, padx=5)
        self.z_key_var = tk.StringVar(value="mz")
        self.z_key_entry = ttk.Entry(control_frame, textvariable=self.z_key_var, width=6)
        self.z_key_entry.grid(row=1, column=5, sticky=tk.W, padx=5)
        
        # Buttons frame
        button_frame = ttk.Frame(control_frame)
        button_frame.grid(row=0, column=6, rowspan=2, padx=10)
        
        self.start_button = ttk.Button(button_frame, text="Start Collection", command=self.start_collection)
        self.start_button.pack(side=tk.LEFT, padx=5)
        
        self.stop_button = ttk.Button(button_frame, text="Stop Collection", command=self.stop_collection, state=tk.DISABLED)
        self.stop_button.pack(side=tk.LEFT, padx=5)
        
        # Calibration frame
        cal_frame = ttk.LabelFrame(main_frame, text="Calibration", padding=10)
        cal_frame.pack(fill=tk.X, pady=5)
        
        self.calibrate_button = ttk.Button(cal_frame, text="Calibrate", command=self.calibrate)
        self.calibrate_button.pack(side=tk.LEFT, padx=5)
        
        self.reset_button = ttk.Button(cal_frame, text="Reset Data", command=self.reset_data)
        self.reset_button.pack(side=tk.LEFT, padx=5)
        
        self.save_button = ttk.Button(cal_frame, text="Save Calibration", command=self.save_calibration)
        self.save_button.pack(side=tk.LEFT, padx=5)
        
        self.load_button = ttk.Button(cal_frame, text="Load Calibration", command=self.load_calibration)
        self.load_button.pack(side=tk.LEFT, padx=5)
        
        # Status frame
        status_frame = ttk.LabelFrame(main_frame, text="Status", padding=10)
        status_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(status_frame, text="Collection:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.collection_status = ttk.Label(status_frame, text="Not collecting")
        self.collection_status.grid(row=0, column=1, sticky=tk.W, padx=5)
        
        ttk.Label(status_frame, text="Points:").grid(row=0, column=2, sticky=tk.W, padx=5)
        self.points_status = ttk.Label(status_frame, text="0")
        self.points_status.grid(row=0, column=3, sticky=tk.W, padx=5)
        
        ttk.Label(status_frame, text="Calibration:").grid(row=0, column=4, sticky=tk.W, padx=5)
        self.calibration_status = ttk.Label(status_frame, text="Not calibrated")
        self.calibration_status.grid(row=0, column=5, sticky=tk.W, padx=5)
        
        ttk.Label(status_frame, text="Quality:").grid(row=0, column=6, sticky=tk.W, padx=5)
        self.quality_status = ttk.Label(status_frame, text="N/A")
        self.quality_status.grid(row=0, column=7, sticky=tk.W, padx=5)
        
        # Results frame
        result_frame = ttk.LabelFrame(main_frame, text="Calibration Results", padding=10)
        result_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(result_frame, text="Hard Iron Offset:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.offset_label = ttk.Label(result_frame, text="[0.000, 0.000, 0.000]")
        self.offset_label.grid(row=0, column=1, sticky=tk.W, padx=5)
        
        # Plot frame
        plot_frame = ttk.Frame(main_frame)
        plot_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        self.canvas_frame = ttk.Frame(plot_frame)
        self.canvas_frame.pack(fill=tk.BOTH, expand=True)
        
    def create_figure(self):
        """Create the matplotlib figure for 3D visualization"""
        self.fig = plt.figure(figsize=(10, 8))
        self.ax = self.fig.add_subplot(111, projection='3d')
        
        # Initialize empty scatter plot
        self.scatter = self.ax.scatter([], [], [], c='b', marker='o', alpha=0.5, s=20)
        
        # Add a sphere to represent the ideal calibrated data
        u = np.linspace(0, 2 * np.pi, 100)
        v = np.linspace(0, np.pi, 100)
        
        sphere_x = np.outer(np.cos(u), np.sin(v))
        sphere_y = np.outer(np.sin(u), np.sin(v))
        sphere_z = np.outer(np.ones(np.size(u)), np.cos(v))
        
        # Plot sphere with low alpha for reference
        self.ax.plot_surface(sphere_x, sphere_y, sphere_z, 
                            color='gray', alpha=0.1, linewidth=0)
        
        # Set labels
        self.ax.set_xlabel('X')
        self.ax.set_ylabel('Y')
        self.ax.set_zlabel('Z')
        
        # Set limits initially
        self.ax.set_xlim(-2, 2)
        self.ax.set_ylim(-2, 2)
        self.ax.set_zlim(-2, 2)
        
        self.ax.set_title('Magnetometer Calibration')
        
        # Add the figure to the tkinter window
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.canvas_frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        
        # Add toolbar
        toolbar_frame = ttk.Frame(self.canvas_frame)
        toolbar_frame.pack(fill=tk.X)
        toolbar = NavigationToolbar2Tk(self.canvas, toolbar_frame)
        toolbar.update()
        
    def update_plot(self, frame):
        """Update the plot with new data"""
        if len(self.data_buffer) < 3:
            return self.scatter,
            
        # Get data from buffer
        data = np.array(list(self.data_buffer))
        
        # Apply calibration if available
        if self.is_calibrated:
            calibrated_data = self.apply_calibration(data)
            
            # Use calibrated data for plotting
            self.scatter._offsets3d = (
                calibrated_data[:, 0], 
                calibrated_data[:, 1], 
                calibrated_data[:, 2]
            )
            
            # Update color to show it's calibrated
            self.scatter.set_color('green')
        else:
            # Use raw data
            self.scatter._offsets3d = (data[:, 0], data[:, 1], data[:, 2])
            self.scatter.set_color('blue')
            
        # Auto-adjust limits if needed
        if frame % 30 == 0:  # Only adjust every 30 frames for performance
            x_range = (np.min(data[:, 0]), np.max(data[:, 0]))
            y_range = (np.min(data[:, 1]), np.max(data[:, 1]))
            z_range = (np.min(data[:, 2]), np.max(data[:, 2]))
            
            max_range = max(
                x_range[1] - x_range[0],
                y_range[1] - y_range[0],
                z_range[1] - z_range[0]
            )
            
            # Add some padding
            padding = max_range * 0.1
            
            # Calculate centers
            x_center = (x_range[0] + x_range[1]) / 2
            y_center = (y_range[0] + y_range[1]) / 2
            z_center = (z_range[0] + z_range[1]) / 2
            
            # Set limits with padding
            self.ax.set_xlim(x_center - max_range/2 - padding, x_center + max_range/2 + padding)
            self.ax.set_ylim(y_center - max_range/2 - padding, y_center + max_range/2 + padding)
            self.ax.set_zlim(z_center - max_range/2 - padding, z_center + max_range/2 + padding)
            
        # Add hard iron offset point if calibrated
        if self.is_calibrated:
            # Check if offset point exists
            offset_point = None
            for artist in self.ax.get_children():
                if hasattr(artist, '_gid') and artist._gid == 'offset_point':
                    offset_point = artist
                    break
                    
            if offset_point is None:
                # Create offset point
                offset_point = self.ax.scatter(
                    [self.offset_x], 
                    [self.offset_y], 
                    [self.offset_z],
                    color='red', 
                    marker='x', 
                    s=100,
                    label='Hard Iron Offset'
                )
                offset_point._gid = 'offset_point'
            else:
                # Update offset point
                offset_point._offsets3d = (
                    [self.offset_x], 
                    [self.offset_y], 
                    [self.offset_z]
                )
                
        # Update title with point count
        self.ax.set_title(f'Magnetometer Calibration - {len(data)} points')
        
        # Update status
        self.update_status()
            
        return self.scatter,
    
    def update_status(self):
        """Update status labels"""
        # Update point count
        self.points_status.config(text=str(len(self.data_buffer)))
        
        # Update collection status
        if self.is_collecting:
            self.collection_status.config(text="Collecting")
        else:
            self.collection_status.config(text="Not collecting")
            
        # Update calibration status
        if self.is_calibrated:
            self.calibration_status.config(text="Calibrated")
            self.quality_status.config(text=f"{self.fit_quality:.2f} ({self.get_quality_description()})")
            self.offset_label.config(text=f"[{self.offset_x:.3f}, {self.offset_y:.3f}, {self.offset_z:.3f}]")
        else:
            self.calibration_status.config(text="Not calibrated")
            self.quality_status.config(text="N/A")
    
    def start_collection(self):
        """Start collecting data from serial port"""
        try:
            port = self.port_var.get()
            baud_rate = self.baud_var.get()
            
            # Open serial port
            self.serial_port = serial.Serial(port, baud_rate, timeout=1)
            self.is_collecting = True
            
            # Update button states
            self.start_button.config(state=tk.DISABLED)
            self.stop_button.config(state=tk.NORMAL)
            
            # Start collection thread
            self.collection_thread = threading.Thread(
                target=self._collect_serial_data, 
                daemon=True
            )
            self.collection_thread.start()
            
            print(f"Started data collection from {port} at {baud_rate} baud")
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to open serial port: {e}")
            self.is_collecting = False
    
    def stop_collection(self):
        """Stop collecting data from serial port"""
        self.is_collecting = False
        
        if self.collection_thread:
            self.collection_thread.join(timeout=1.0)
            
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            
        # Update button states
        self.start_button.config(state=tk.NORMAL)
        self.stop_button.config(state=tk.DISABLED)
        
        print("Stopped data collection")
    
    def _collect_serial_data(self):
        """Thread function to collect data from serial port"""
        while self.is_collecting and self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting > 0:
                    line = self.serial_port.readline().decode('utf-8').strip()
                    
                    # Parse data based on format
                    data_point = self._parse_serial_data(line)
                    
                    if data_point is not None:
                        self.data_buffer.append(data_point)
                else:
                    time.sleep(0.01)  # Small delay to prevent CPU hogging
            except Exception as e:
                print(f"Error reading serial data: {e}")
                time.sleep(0.1)
    
    def _parse_serial_data(self, line):
        """Parse a line of serial data based on the specified format"""
        try:
            if self.format_var.get() == 'json':
                # Parse JSON format
                data = json.loads(line)
                x = float(data[self.x_key_var.get()])
                y = float(data[self.y_key_var.get()])
                z = float(data[self.z_key_var.get()])
                return [x, y, z]
                
            elif self.format_var.get() == 'csv':
                # Parse CSV format
                values = line.split(',')
                if len(values) >= 3:
                    x = float(values[0])
                    y = float(values[1])
                    z = float(values[2])
                    return [x, y, z]
                
            return None
        except Exception as e:
            # Silently ignore parsing errors - they're common in serial data
            return None
    
    def calibrate(self):
        """Perform calibration on the collected data"""
        if len(self.data_buffer) < 10:
            messagebox.showwarning("Warning", "Not enough data points for calibration. Collect more data.")
            return
            
        try:
            # Convert buffer to numpy array
            data = np.array(list(self.data_buffer))
            
            # Initial guess
            initial_offset = np.mean(data, axis=0)
            initial_W = np.eye(3).flatten()[0:6]  # Only need 6 parameters for symmetric matrix
            initial_params = np.concatenate([initial_offset, initial_W])
            
            # Optimize
            result = optimize.minimize(
                self._objective_function, 
                initial_params,
                args=(data,),
                method='Powell',
                options={'maxiter': 1000}
            )
            
            if result.success:
                # Extract the optimized parameters
                self.offset_x, self.offset_y, self.offset_z = result.x[0:3]
                W_flat = result.x[3:9]
                
                # Reconstruct the W matrix
                self.W = np.array([
                    [W_flat[0], W_flat[1], W_flat[2]],
                    [W_flat[1], W_flat[3], W_flat[4]], 
                    [W_flat[2], W_flat[4], W_flat[5]]
                ])
                
                # Calculate fit quality metric (from 0.0 to 1.0)
                raw_magnitudes = np.linalg.norm(data, axis=1)
                cal_magnitudes = np.linalg.norm(self.apply_calibration(data), axis=1)
                
                raw_cv = np.std(raw_magnitudes) / np.mean(raw_magnitudes)  # Coefficient of variation
                cal_cv = np.std(cal_magnitudes) / np.mean(cal_magnitudes)
                
                # Calculate improvement
                if raw_cv > 0:
                    self.fit_quality = min(1.0, max(0.0, 1.0 - cal_cv/raw_cv))
                else:
                    self.fit_quality = 0.0
                    
                self.is_calibrated = True
                print(f"Calibration completed successfully. Fit quality: {self.fit_quality:.2f}")
                print(f"Hard iron offset: [{self.offset_x:.6f}, {self.offset_y:.6f}, {self.offset_z:.6f}]")
                
                # Update status
                self.update_status()
            else:
                messagebox.showerror("Error", "Calibration failed")
                
        except Exception as e:
            messagebox.showerror("Error", f"Error during calibration: {e}")
    
    def _objective_function(self, params, data):
        """Objective function for optimization"""
        # Extract parameters
        offset = params[0:3]
        W_flat = params[3:9]
        
        # Reconstruct the W matrix (only 6 parameters for symmetric matrix)
        W = np.array([
            [W_flat[0], W_flat[1], W_flat[2]],
            [W_flat[1], W_flat[3], W_flat[4]], 
            [W_flat[2], W_flat[4], W_flat[5]]
        ])
        
        # Apply calibration
        centered_data = data - offset
        calibrated = np.dot(centered_data, W)
        
        # Calculate magnitudes
        magnitudes = np.linalg.norm(calibrated, axis=1)
        
        # We want these magnitudes to be as close to the reference field as possible
        # and with minimal variance
        mean_magnitude = np.mean(magnitudes)
        variance = np.var(magnitudes / mean_magnitude)
        
        # Also penalize if the mean magnitude is far from the reference field
        scale_penalty = (mean_magnitude - self.field_reference)**2
        
        return variance + 0.1 * scale_penalty
    
    def apply_calibration(self, data):
        """Apply calibration to data"""
        if not self.is_calibrated:
            return data
            
        offset = np.array([self.offset_x, self.offset_y, self.offset_z])
        centered_data = data - offset
        return np.dot(centered_data, self.W)
    
    def reset_data(self):
        """Clear all data and reset calibration"""
        if messagebox.askyesno("Reset", "Are you sure you want to reset all data and calibration?"):
            self.data_buffer.clear()
            self.is_calibrated = False
            self.offset_x = 0.0
            self.offset_y = 0.0
            self.offset_z = 0.0
            self.W = np.eye(3)
            self.fit_quality = 0.0
            
            # Update status
            self.update_status()
    
    def save_calibration(self):
        """Save calibration parameters to a file"""
        if not self.is_calibrated:
            messagebox.showwarning("Warning", "No calibration to save")
            return
            
        try:
            file_path = filedialog.asksaveasfilename(
                defaultextension=".npz",
                filetypes=[("NumPy files", "*.npz"), ("All files", "*.*")]
            )
            
            if not file_path:
                return
                
            np.savez(
                file_path,
                hard_iron_offset=np.array([self.offset_x, self.offset_y, self.offset_z]),
                soft_iron_matrix=self.W,
                fit_quality=self.fit_quality
            )
            
            messagebox.showinfo("Success", f"Calibration saved to {file_path}")
            
        except Exception as e:
            messagebox.showerror("Error", f"Error saving calibration: {e}")
    
    def load_calibration(self):
        """Load calibration parameters from a file"""
        try:
            file_path = filedialog.askopenfilename(
                filetypes=[("NumPy files", "*.npz"), ("All files", "*.*")]
            )
            
            if not file_path:
                return
                
            data = np.load(file_path)
            self.offset_x, self.offset_y, self.offset_z = data['hard_iron_offset']
            self.W = data['soft_iron_matrix']
            self.fit_quality = float(data['fit_quality'])
            self.is_calibrated = True
            
            # Update status
            self.update_status()
            
            messagebox.showinfo("Success", f"Calibration loaded from {file_path}")
            
        except Exception as e:
            messagebox.showerror("Error", f"Error loading calibration: {e}")
    
    def get_quality_description(self):
        """Get a description of the fit quality"""
        if not self.is_calibrated:
            return "Not calibrated"
            
        if self.fit_quality > 0.95:
            return "Excellent"
        elif self.fit_quality > 0.85:
            return "Good"
        elif self.fit_quality > 0.7:
            return "Fair"
        elif self.fit_quality > 0.5:
            return "Poor"
        else:
            return "Very poor"


def generate_test_data(num_points=1000, 
                      hard_iron_offset=(0.5, -0.7, 0.2), 
                      soft_iron_scale=(1.2, 0.9, 1.1),
                      noise_level=0.05):
    """Generate synthetic magnetometer data for testing"""
    # Generate points on a unit sphere
    theta = np.random.uniform(0, 2*np.pi, num_points)
    phi = np.random.uniform(0, np.pi, num_points)
    
    x = np.sin(phi) * np.cos(theta)
    y = np.sin(phi) * np.sin(theta)
    z = np.cos(phi)
    
    # Apply soft iron distortion (scaling)
    x *= soft_iron_scale[0]
    y *= soft_iron_scale[1]
    z *= soft_iron_scale[2]
    
    # Apply hard iron offset
    x += hard_iron_offset[0]
    y += hard_iron_offset[1]
    z += hard_iron_offset[2]
    
    # Add noise
    x += np.random.normal(0, noise_level, num_points)
    y += np.random.normal(0, noise_level, num_points)
    z += np.random.normal(0, noise_level, num_points)
    
    return np.column_stack([x, y, z])


def simulate_serial_port(port, data_interval=0.05):
    """Simulate a serial port with magnetometer data for testing"""
    # Generate some test data
    data = generate_test_data(num_points=1000)
    
    try:
        # Create a thread to send simulated data
        def send_data():
            with serial.Serial(port, 115200, timeout=1) as ser:
                print(f"Sending simulated data on {port}")
                
                idx = 0
                while True:
                    # Cycle through the data
                    point = data[idx % len(data)]
                    idx += 1
                    
                    # JSON format
                    json_data = json.dumps({"mx": float(point[0]), "my": float(point[1]), "mz": float(point[2])})
                    ser.write(f"{json_data}\n".encode('utf-8'))
                    
                    time.sleep(data_interval)
        
        thread = threading.Thread(target=send_data, daemon=True)
        thread.start()
        
        return True
    except Exception as e:
        print(f"Error creating simulated serial port: {e}")
        return False


def main():
    root = tk.Tk()
    app = MagnetometerCalibrator(root)
    
    # If you want to test with simulated data, uncomment these lines:
    # from serial.tools.list_ports import comports
    # ports = [p.device for p in comports()]
    # if ports:
    #     simulate_serial_port(ports[0])
    
    root.mainloop()


if __name__ == "__main__":
    main()