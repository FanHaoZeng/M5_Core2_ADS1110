import sounddevice as sd
import numpy as np
import wave
import time
from datetime import datetime
import threading
import os

class AudioManager:
    def __init__(self):
        # 采样率 / Sample rate
        self.sample_rate = 44100
        self.recording = False
        self.playing = False
        self.recorded_data = []
        
        # 创建recordings文件夹（如果不存在）/ Create recordings directory if not exists
        self.recordings_dir = "recordings"
        if not os.path.exists(self.recordings_dir):
            os.makedirs(self.recordings_dir)
    
    def generate_tone(self, frequency, duration, amplitude=0.5):
        """生成指定频率的正弦波 / Generate sine wave with specified frequency"""
        t = np.linspace(0, duration, int(self.sample_rate * duration), False)
        tone = amplitude * np.sin(2 * np.pi * frequency * t)
        return tone.astype(np.float32)
    
    def play_tone(self, frequency, duration):
        """播放指定频率的声音 / Play tone with specified frequency"""
        try:
            tone = self.generate_tone(frequency, duration)
            self.playing = True
            sd.play(tone, self.sample_rate)
            sd.wait()  # 等待播放完成 / Wait for playback to complete
            self.playing = False
        except Exception as e:
            print(f"Error playing sound: {e}")
            self.playing = False
    
    def record_audio(self, duration, filename):
        """录制音频 / Record audio"""
        try:
            print(f"Starting recording, duration: {duration} seconds")
            self.recording = True
            
            # 录制音频 / Record audio
            recording = sd.rec(
                int(duration * self.sample_rate),
                samplerate=self.sample_rate,
                channels=1
            )
            sd.wait()  # 等待录制完成 / Wait for recording to complete
            self.recording = False
            
            # 完整的文件路径 / Complete file path
            filepath = os.path.join(self.recordings_dir, filename)
            
            # 保存录音 / Save recording
            with wave.open(filepath, 'wb') as wf:
                wf.setnchannels(1)  # 单声道 / Mono channel
                wf.setsampwidth(2)  # 2字节采样宽度 / 2-byte sample width
                wf.setframerate(self.sample_rate)
                wf.writeframes((recording * 32767).astype(np.int16).tobytes())
            
            print(f"Recording completed, file saved: {filepath}")
            
        except Exception as e:
            print(f"Error recording audio: {e}")
            self.recording = False
    
    def play_and_record(self, frequency, duration, record_duration=None):
        """同时播放和录制音频 / Simultaneously play and record audio"""
        if record_duration is None:
            record_duration = duration + 1  # 默认多录制1秒 / Default to record 1 extra second
            
        # 生成文件名（使用时间戳）/ Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"audio_record_{timestamp}_{frequency}Hz.wav"
        
        try:
            # 创建录制线程 / Create recording thread
            record_thread = threading.Thread(
                target=self.record_audio,
                args=(record_duration, filename)
            )
            
            # 创建播放线程 / Create playback thread
            play_thread = threading.Thread(
                target=self.play_tone,
                args=(frequency, duration)
            )
            
            # 启动录制 / Start recording
            record_thread.start()
            time.sleep(0.5)  # 等待录制启动 / Wait for recording to start
            
            # 启动播放 / Start playback
            play_thread.start()
            
            # 等待两个线程完成 / Wait for both threads to complete
            play_thread.join()
            record_thread.join()
            
        except Exception as e:
            print(f"Error during play and record: {e}")

def main():
    # 创建音频管理器实例 / Create audio manager instance
    audio_mgr = AudioManager()
    
    try:
        while True:
            print("\n=== Audio Playback and Recording System ===")
            print("1. Play tone with specified frequency")
            print("2. Record environmental sound")
            print("3. Play and record simultaneously")
            print("4. Exit")
            
            choice = input("\nSelect operation (1-4): ")
            
            if choice == '1':
                freq = float(input("Enter frequency (Hz): "))
                duration = float(input("Enter duration (seconds): "))
                print(f"\nPlaying {freq}Hz tone...")
                audio_mgr.play_tone(freq, duration)
                
            elif choice == '2':
                duration = float(input("Enter recording duration (seconds): "))
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"environment_record_{timestamp}.wav"
                audio_mgr.record_audio(duration, filename)
                
            elif choice == '3':
                freq = float(input("Enter frequency (Hz): "))
                duration = float(input("Enter playback duration (seconds): "))
                record_duration = float(input("Enter recording duration (seconds): "))
                print(f"\nPlaying {freq}Hz tone and recording...")
                audio_mgr.play_and_record(freq, duration, record_duration)
                
            elif choice == '4':
                print("\nExiting program")
                break
                
            else:
                print("\nInvalid choice, please try again")
                
    except KeyboardInterrupt:
        print("\nProgram interrupted by user")
    except Exception as e:
        print(f"An error occurred: {e}")
    finally:
        # 确保所有音频操作都已停止 / Ensure all audio operations are stopped
        sd.stop()

if __name__ == "__main__":
    main() 