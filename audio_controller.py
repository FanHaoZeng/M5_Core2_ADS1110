import sounddevice as sd
import numpy as np
import wave
import time
from datetime import datetime
import threading
import os
import csv
import pandas as pd

class AudioManager:
    def __init__(self):
        # 采样率 / Sample rate
        self.sample_rate = 44100
        self.recording = False
        self.playing = False
        self.recorded_data = []
        
        # 创建必要的目录 / Create necessary directories
        self.recordings_dir = "recordings"
        self.logs_dir = "logs"
        for directory in [self.recordings_dir, self.logs_dir]:
            if not os.path.exists(directory):
                os.makedirs(directory)
        
        # 初始化日志文件 / Initialize log file
        self.log_file = os.path.join(self.logs_dir, f"audio_log_{datetime.now().strftime('%Y%m%d')}.csv")
        self.initialize_log_file()
        
        self.recording_event = threading.Event()  # 添加事件控制
        self.playback_event = threading.Event()
    
    def initialize_log_file(self):
        """初始化CSV日志文件 / Initialize CSV log file"""
        if not os.path.exists(self.log_file):
            with open(self.log_file, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    'StartTime',
                    'EndTime',
                    'Frequency',
                    'Duration',
                    'Waveform',
                    'Filename',
                    'Status'
                ])

    def get_timestamp(self):
        """获取格式化的时间戳 / Get formatted timestamp"""
        now = datetime.now()
        return now.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"

    def log_operation(self, frequency, duration, waveform, filename, status="Success"):
        """记录操作到CSV / Log operation to CSV"""
        try:
            end_time = self.get_timestamp()
            with open(self.log_file, 'a', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([
                    self.start_time,
                    end_time,
                    "{:.1f}".format(frequency),
                    "{:.1f}".format(duration),
                    waveform,
                    filename,
                    status
                ])
        except Exception as e:
            print(f"Warning: Could not write to log file: {str(e)}")

    def generate_waveform(self, frequency, duration, waveform='sine', amplitude=0.5):
        """生成指定波形的信号 / Generate signal with specified waveform"""
        t = np.linspace(0, duration, int(self.sample_rate * duration), False)
        
        if waveform == 'sine':
            signal = amplitude * np.sin(2 * np.pi * frequency * t)
        elif waveform == 'square':
            signal = amplitude * np.sign(np.sin(2 * np.pi * frequency * t))
        elif waveform == 'triangle':
            signal = amplitude * (2/np.pi) * np.arcsin(np.sin(2 * np.pi * frequency * t))
        elif waveform == 'sawtooth':
            signal = amplitude * (2/np.pi) * np.arctan(np.tan(np.pi * frequency * t))
        else:
            signal = amplitude * np.sin(2 * np.pi * frequency * t)
            
        return signal.astype(np.float32)
    
    def play_tone(self, frequency, duration, waveform='sine'):
        """播放指定波形的声音 / Play tone with specified waveform"""
        try:
            tone = self.generate_waveform(frequency, duration, waveform)
            self.playing = True
            
            # 通知录制线程准备就绪
            self.playback_event.set()
            
            # 等待录制线程准备完成
            self.recording_event.wait()
            
            # 开始播放
            sd.play(tone, self.sample_rate)
            sd.wait()
            self.playing = False
            
        except Exception as e:
            print(f"Error playing sound: {e}")
            self.playing = False
    
    def record_audio(self, duration, filename):
        """录制音频 / Record audio"""
        try:
            # 等待播放准备就绪
            self.playback_event.wait()
            
            print(f"Starting recording, duration: {duration} seconds")
            self.recording = True
            
            recording = sd.rec(
                int(duration * self.sample_rate),
                samplerate=self.sample_rate,
                channels=1
            )
            
            # 通知播放线程可以开始了
            self.recording_event.set()
            
            # 等待录制完成
            sd.wait()
            self.recording = False
            
            filepath = os.path.join(self.recordings_dir, filename)
            
            # 保存录音
            with wave.open(filepath, 'wb') as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(self.sample_rate)
                wf.writeframes((recording * 32767).astype(np.int16).tobytes())
            
            print(f"Recording completed, file saved: {filepath}")
            
        except Exception as e:
            print(f"Error recording audio: {e}")
            self.recording = False
        finally:
            # 重置事件状态
            self.recording_event.clear()
            self.playback_event.clear()
    
    def play_and_record(self, frequency, duration, waveform='sine'):
        """同时播放和录制音频 / Simultaneously play and record audio"""
        try:
            # 记录开始时间
            self.start_time = self.get_timestamp()
            
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = "audio_record_{}_{:.1f}Hz_{}.wav".format(
                timestamp, frequency, waveform)
            
            # 重置事件状态
            self.recording_event.clear()
            self.playback_event.clear()
            
            # 创建录制线程
            record_thread = threading.Thread(
                target=self.record_audio,
                args=(duration, filename)
            )
            
            # 创建播放线程
            play_thread = threading.Thread(
                target=self.play_tone,
                args=(frequency, duration, waveform)
            )
            
            # 启动两个线程
            record_thread.start()
            play_thread.start()
            
            # 等待两个线程完成
            play_thread.join()
            record_thread.join()
            
            # 记录操作日志
            self.log_operation(frequency, duration, waveform, filename)
            
        except Exception as e:
            print(f"Error during play and record: {e}")
            try:
                self.log_operation(frequency, duration, waveform, filename, f"Failed: {str(e)}")
            except:
                print("Could not log error to file")

def main():
    # 创建音频管理器实例 / Create audio manager instance
    audio_mgr = AudioManager()
    
    try:
        while True:
            print("\n=== Audio Generator and Recorder ===")
            print("Available waveforms: sine, square, triangle, sawtooth")
            print("Enter 'q' to quit")
            
            waveform = input("\nSelect waveform (default: sine): ").lower()
            if waveform == 'q':
                break
            if not waveform:
                waveform = 'sine'
            
            if waveform not in ['sine', 'square', 'triangle', 'sawtooth']:
                print("Invalid waveform, using sine wave")
                waveform = 'sine'
            
            try:
                freq = float(input("Enter frequency (Hz): "))
                duration = float(input("Enter duration (seconds): "))
                
                print(f"\nPlaying and recording {freq}Hz {waveform} wave...")
                audio_mgr.play_and_record(freq, duration, waveform)
                
            except ValueError:
                print("Invalid input. Please enter numeric values.")
                
    except KeyboardInterrupt:
        print("\nProgram interrupted by user")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        # 确保所有音频操作都已停止 / Ensure all audio operations are stopped
        sd.stop()

if __name__ == "__main__":
    main() 