import { Inject, Injectable } from '@nestjs/common';
import { SOCIAL, SocialConfig } from './social.config';

// Hook kiểm duyệt: hiện là lọc từ cấm đơn giản (data/social/social.json). Có thể thay bằng dịch vụ kiểm duyệt
// bên ngoài mà không đổi chỗ gọi (chat, tên bang, mô tả bang).
@Injectable()
export class ModerationService {
  private readonly patterns: RegExp[];
  constructor(@Inject(SOCIAL) cfg: SocialConfig) {
    const esc = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    // Biên từ theo Unicode (chữ có dấu tiếng Việt), không bắt nhầm từ chứa chuỗi con
    this.patterns = cfg.chat.banned_words.map((w) => new RegExp(`(?<![\\p{L}\\p{N}])${esc(w.normalize('NFC'))}(?![\\p{L}\\p{N}])`, 'giu'));
  }
  containsBanned(text: string) {
    const t = text.normalize('NFC');
    return this.patterns.some((p) => {
      p.lastIndex = 0;
      return p.test(t);
    });
  }
  mask(text: string) {
    let t = text.normalize('NFC');
    for (const p of this.patterns) t = t.replace(p, (m) => '*'.repeat([...m].length));
    return t;
  }
}

// Loại ký tự điều khiển / định dạng vô hình (chống giả mạo hiển thị, chèn xuống dòng hàng loạt).
export function sanitizeText(raw: string) {
  return raw.normalize('NFC').replace(/[\p{Cc}\p{Cf}]/gu, ' ').replace(/\s+/g, ' ').trim();
}
