import { canAct, GUILD_PERMISSIONS, GuildAction, GuildRole, permissionsOf } from './guild.permissions';

const roles: GuildRole[] = ['OWNER', 'OFFICER', 'MEMBER'];

describe('ma trận quyền guild', () => {
  it('MEMBER không có quyền quản trị nào', () => {
    expect(permissionsOf('MEMBER')).toEqual(['leave']);
  });

  it('không ai kick/hạ cấp người cùng cấp hoặc cao hơn', () => {
    for (const a of roles) for (const t of roles) {
      if (a === 'OWNER' && t !== 'OWNER') continue;
      if (a === 'OFFICER' && t === 'MEMBER') continue;
      expect(canAct(a, 'kick', t)).toBe(false);
    }
    expect(canAct('OWNER', 'kick', 'OFFICER')).toBe(true);
    expect(canAct('OFFICER', 'kick', 'MEMBER')).toBe(true);
    expect(canAct('OFFICER', 'kick', 'OFFICER')).toBe(false);
  });

  it('chỉ OWNER thăng/giáng cấp, chuyển quyền, đổi tên, giải tán', () => {
    for (const r of ['OFFICER', 'MEMBER'] as GuildRole[]) {
      expect(canAct(r, 'promote', 'MEMBER')).toBe(false);
      expect(canAct(r, 'demote', 'OFFICER')).toBe(false);
      expect(canAct(r, 'transfer', 'MEMBER')).toBe(false);
      expect(canAct(r, 'rename')).toBe(false);
      expect(canAct(r, 'disband')).toBe(false);
    }
    expect(canAct('OWNER', 'promote', 'MEMBER')).toBe(true);
    expect(canAct('OWNER', 'promote', 'OFFICER')).toBe(false);  // không có cấp nào trên OFFICER ngoài OWNER (dùng transfer)
    expect(canAct('OWNER', 'demote', 'MEMBER')).toBe(false);
    expect(canAct('OWNER', 'transfer', 'OWNER')).toBe(false);
  });

  it('OWNER không được "leave" (phải chuyển quyền hoặc giải tán)', () => {
    expect(canAct('OWNER', 'leave')).toBe(false);
  });

  it('hành động nhắm người mà thiếu target -> từ chối; hành động thường có target -> từ chối', () => {
    for (const r of roles) {
      expect(canAct(r, 'kick')).toBe(false);
      expect(canAct(r, 'invite', 'MEMBER')).toBe(false);
    }
  });

  it('mọi hành động đều được khai báo', () => {
    const all: GuildAction[] = ['view_requests', 'invite', 'review_join', 'edit_profile', 'rename', 'kick', 'promote', 'demote', 'transfer', 'disband', 'leave'];
    expect(Object.keys(GUILD_PERMISSIONS).sort()).toEqual([...all].sort());
  });
});
