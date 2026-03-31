import cv2
import numpy as np
import sys
import os

def convert_to_cvid(input_video, output_file, target_fps=25):
    if not os.path.exists(input_video):
        print(f"файл {input_video} не найден")
        return

    cap = cv2.VideoCapture(input_video)
    source_fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    duration = total_frames / source_fps if source_fps > 0 else 0
    
    frame_skip = max(1, round(source_fps / target_fps))
    actual_fps = source_fps / frame_skip
    
    
    with open(output_file, "wb") as out:
        #1байт = фпс
        out.write(np.uint8(int(actual_fps)))
        
        frame_idx = 0
        written_frames = 0
        while cap.isOpened():
            ret, frame = cap.read()
            if not ret: break
            
            frame_idx += 1

            if frame_idx % frame_skip != 0:
                continue
            
            #ресайз
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            resized = cv2.resize(gray, (128, 64), interpolation=cv2.INTER_AREA)
            _, bw = cv2.threshold(resized, 128, 1, cv2.THRESH_BINARY)
            
            #переупаковываем под олег
            buffer = []
            for x in range(128):
                for page in range(8):
                    byte = 0
                    for bit in range(8):
                        if bw[page*8 + bit, x]:
                            byte |= (1 << bit)
                    buffer.append(byte)
            
            #и сжатие
            bits = []
            for b in buffer:
                for i in range(8):
                    bits.append((b >> i) & 1)
            
            current_bit = bits[0]
            cnt = 0
            for bit in bits:
                if bit == current_bit and cnt < 127:
                    cnt += 1
                else:
                    out.write(np.uint8((current_bit << 7) | cnt))
                    current_bit = bit
                    cnt = 1
            out.write(np.uint8((current_bit << 7) | cnt))
            
            #маркер конца кадра
            out.write(np.uint8(0)) 
            
            written_frames += 1
            if written_frames % 100 == 0:
                print(f"кадров: {written_frames}")

    cap.release()
    size_kb = os.path.getsize(output_file) / 1024
    print(f"записано кадров: {written_frames}")
    print(f"FPS: {int(actual_fps)}")
    print(f"размер: {size_kb:.1f} КБ ({size_kb/1024:.2f} МБ)")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("использование: python converter.py <input_video> [output_file] [fps]")
        print("fps по умолчанию: 25")
        sys.exit(1)
    
    input_vid = sys.argv[1]
    output_vid = sys.argv[2] if len(sys.argv) > 2 else "video.catvid"
    target_fps = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    
    convert_to_cvid(input_vid, output_vid, target_fps)
