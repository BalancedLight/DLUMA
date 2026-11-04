using System;
using System.ComponentModel;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;

namespace Dluma.EditorTheme.Framework
{
    internal sealed class WinFormsThemeAdapter
    {
        private readonly Control control;
        private readonly Color originalBack, originalFore;
        private readonly bool backSerialized, foreSerialized;
        private Color appliedBack, appliedFore;
        private bool changedBack, changedFore, restored;
        private readonly ToolStrip strip;
        private readonly ToolStripRenderer originalRenderer;
        private readonly ToolStripRenderMode originalRenderMode;
        private ToolStripRenderer appliedRenderer;
        private readonly PropertyGrid grid;
        private readonly Color[] gridOriginal, gridApplied;
        private ThemeSnapshot current;
        private bool ownsTreeDraw, ownsListDraw, ownsComboDraw, ownsTabDraw;
        private bool listOriginalGridLines, listGridLinesChanged;
        private bool listOriginalDoubleBuffered, listDoubleBufferChanged;
        private static readonly PropertyInfo ListDoubleBuffered = typeof(ListView).GetProperty(
            "DoubleBuffered", BindingFlags.Instance | BindingFlags.NonPublic);
        private readonly ManagedListFrame listFrame;

        internal WinFormsThemeAdapter(Control value)
        {
            control = value;
            originalBack = value.BackColor; originalFore = value.ForeColor;
            backSerialized = IsSerialized(value, "BackColor"); foreSerialized = IsSerialized(value, "ForeColor");
            strip = value as ToolStrip;
            if (strip != null) { originalRenderer = strip.Renderer; originalRenderMode = strip.RenderMode; }
            grid = value as PropertyGrid;
            if (grid != null) { gridOriginal = GetGridColors(grid); gridApplied = new Color[gridOriginal.Length]; }
            ListView list = value as ListView;
            if (list != null && list.GetType() == typeof(ListView) && !list.OwnerDraw &&
                !list.CheckBoxes && list.SmallImageList == null && list.LargeImageList == null)
                listFrame = new ManagedListFrame(list);
            control.ControlAdded += ChildAdded;
            control.HandleCreated += HandleCreated;
            control.Disposed += Disposed;
        }

        internal void Apply(ThemeSnapshot theme)
        {
            if (restored || control.IsDisposed) return;
            current = theme;
            if (listFrame != null) listFrame.Apply(theme);
            if (CanTheme(control))
            {
                Color back = theme.Colors[UsesSurface(control) ? 2 : 1];
                Color fore = theme.Colors[4];
                bool inheritedPanel = control is Panel && control.GetType() != typeof(Panel);
                if (!changedBack && (!inheritedPanel || !backSerialized) && IsOrdinary(originalBack)) changedBack = true;
                if (!changedFore && (!inheritedPanel || !foreSerialized) && IsOrdinary(originalFore)) changedFore = true;
                if (changedBack && (control.BackColor == originalBack || control.BackColor == appliedBack)) { control.BackColor = back; appliedBack = back; }
                if (changedFore && (control.ForeColor == originalFore || control.ForeColor == appliedFore)) { control.ForeColor = fore; appliedFore = fore; }
            }
            if (grid != null)
            {
                Color[] desired = { theme.Colors[2], theme.Colors[4], theme.Colors[1], theme.Colors[4], theme.Colors[4], theme.Colors[4], theme.Colors[1], theme.Colors[8], theme.Colors[10], theme.Colors[10], theme.Colors[7] };
                Color[] existing = GetGridColors(grid);
                for (int i = 0; i < desired.Length; ++i)
                    if ((IsOrdinary(gridOriginal[i]) && existing[i] == gridOriginal[i]) || (!gridApplied[i].IsEmpty && existing[i] == gridApplied[i])) { SetGridColor(grid, i, desired[i]); gridApplied[i] = desired[i]; }
            }
            if (strip != null && originalRenderMode != ToolStripRenderMode.Custom &&
                (originalRenderer.GetType() == typeof(ToolStripProfessionalRenderer) || originalRenderer.GetType() == typeof(ToolStripSystemRenderer)) &&
                (appliedRenderer == null ? strip.Renderer == originalRenderer : strip.Renderer == appliedRenderer))
            {
                appliedRenderer = new ThemeStripRenderer(theme);
                strip.Renderer = appliedRenderer;
            }
            InstallDrawHandlers();
            control.Invalidate();
        }

        internal void Restore()
        {
            if (restored) return;
            restored = true;
            control.ControlAdded -= ChildAdded; control.HandleCreated -= HandleCreated; control.Disposed -= Disposed;
            if (listFrame != null) listFrame.Dispose();
            if (control.IsDisposed) return;
            RemoveDrawHandlers();
            if (changedBack && control.BackColor == appliedBack) { if (backSerialized) control.BackColor = originalBack; else control.ResetBackColor(); }
            if (changedFore && control.ForeColor == appliedFore) { if (foreSerialized) control.ForeColor = originalFore; else control.ResetForeColor(); }
            if (strip != null && strip.Renderer == appliedRenderer)
            {
                if (originalRenderMode == ToolStripRenderMode.Custom) strip.Renderer = originalRenderer;
                else strip.RenderMode = originalRenderMode;
            }
            if (grid != null)
            {
                Color[] existing = GetGridColors(grid);
                for (int i = 0; i < existing.Length; ++i) if (!gridApplied[i].IsEmpty && existing[i] == gridApplied[i]) SetGridColor(grid, i, gridOriginal[i]);
            }
            control.Invalidate();
        }
        private void ChildAdded(object sender, ControlEventArgs e) { ThemeHost.TrackTree(e.Control); }
        private void HandleCreated(object sender, EventArgs e) { ThemeHost.TrackTree(control); }
        private void Disposed(object sender, EventArgs e) { Restore(); ThemeHost.Forget(control); }
        private static bool IsSerialized(Control c, string property) { PropertyDescriptor p = TypeDescriptor.GetProperties(c)[property]; return p != null && p.ShouldSerializeValue(c); }
        internal static bool IsOrdinary(Color color)
        {
            if (color.IsEmpty || color.IsSystemColor) return true;
            return color.A == 255 && color.R == color.G && color.G == color.B &&
                (color.R == 0 || color.R == 255 || color.R == 192 || color.R == 240);
        }
        private static bool CanTheme(Control c)
        {
            if (c is PictureBox || c is NumericUpDown || c is DomainUpDown) return false;
            return c is Form || c is Panel || c is GroupBox || c is Label || c is TextBoxBase || c is ButtonBase ||
                   c is ComboBox || c is ListBox || c is ListView || c is TreeView || c is TabControl || c is TabPage ||
                   c is PropertyGrid || c is ToolStrip || c is SplitContainer || c is Splitter;
        }
        private static bool UsesSurface(Control c) { return c is TextBoxBase || c is ListBox || c is ListView || c is TreeView || c is ComboBox || c is PropertyGrid; }

        private void InstallDrawHandlers()
        {
            TreeView tree = control as TreeView;
            if (tree != null && tree.GetType() == typeof(TreeView) && !ownsTreeDraw && tree.DrawMode == TreeViewDrawMode.Normal)
            { ownsTreeDraw = true; tree.DrawNode += DrawTree; tree.DrawMode = TreeViewDrawMode.OwnerDrawText; }
            ListView list = control as ListView;
            if (ownsListDraw && list != null && current.Kind == 2) RemoveListDrawHandlers(list);
            if (list != null && current.Kind != 2 && list.GetType() == typeof(ListView) && !ownsListDraw && !list.OwnerDraw && list.SmallImageList == null && list.LargeImageList == null && !list.CheckBoxes && list.View == View.Details)
            {
                listOriginalGridLines = list.GridLines;
                listGridLinesChanged = false;
                list.BeginUpdate();
                try
                {
                    if (ListDoubleBuffered == null) return;
                    listOriginalDoubleBuffered = (bool)ListDoubleBuffered.GetValue(list, null);
                    if (!listOriginalDoubleBuffered)
                    {
                        listDoubleBufferChanged = true;
                        ListDoubleBuffered.SetValue(list, true, null);
                    }
                    ownsListDraw = true;
                    list.DrawColumnHeader += DrawListHeader; list.DrawItem += DrawListItem; list.DrawSubItem += DrawListSubItem;
                    list.OwnerDraw = true;
                    if (listOriginalGridLines && list.GridLines)
                    {
                        listGridLinesChanged = true;
                        list.GridLines = false;
                    }
                }
                catch
                {
                    list.DrawColumnHeader -= DrawListHeader; list.DrawItem -= DrawListItem; list.DrawSubItem -= DrawListSubItem;
                    if (list.OwnerDraw) list.OwnerDraw = false;
                    if (listGridLinesChanged && !list.GridLines) list.GridLines = listOriginalGridLines;
                    listGridLinesChanged = false;
                    ownsListDraw = false;
                    RestoreListBuffering(list);
                    throw;
                }
                finally { list.EndUpdate(); }
            }
            ComboBox combo = control as ComboBox;
            if (combo != null && combo.GetType() == typeof(ComboBox) && !ownsComboDraw && combo.DrawMode == DrawMode.Normal && combo.DropDownStyle == ComboBoxStyle.DropDownList)
            { ownsComboDraw = true; combo.DrawItem += DrawCombo; combo.DrawMode = DrawMode.OwnerDrawFixed; }
            TabControl tabs = control as TabControl;
            if (tabs != null && tabs.GetType() == typeof(TabControl) && !ownsTabDraw && tabs.DrawMode == TabDrawMode.Normal && tabs.ImageList == null)
            { ownsTabDraw = true; tabs.DrawItem += DrawTab; tabs.DrawMode = TabDrawMode.OwnerDrawFixed; }
        }
        private void RemoveDrawHandlers()
        {
            TreeView tree = control as TreeView;
            if (ownsTreeDraw && tree != null) { tree.DrawNode -= DrawTree; if (tree.DrawMode == TreeViewDrawMode.OwnerDrawText) tree.DrawMode = TreeViewDrawMode.Normal; }
            ListView list = control as ListView;
            if (ownsListDraw && list != null) RemoveListDrawHandlers(list);
            ComboBox combo = control as ComboBox;
            if (ownsComboDraw && combo != null) { combo.DrawItem -= DrawCombo; if (combo.DrawMode == DrawMode.OwnerDrawFixed) combo.DrawMode = DrawMode.Normal; }
            TabControl tabs = control as TabControl;
            if (ownsTabDraw && tabs != null) { tabs.DrawItem -= DrawTab; if (tabs.DrawMode == TabDrawMode.OwnerDrawFixed) tabs.DrawMode = TabDrawMode.Normal; }
        }
        private void RestoreListBuffering(ListView list)
        {
            if (!listDoubleBufferChanged) return;
            if ((bool)ListDoubleBuffered.GetValue(list, null))
                ListDoubleBuffered.SetValue(list, listOriginalDoubleBuffered, null);
            listDoubleBufferChanged = false;
        }
        private void RemoveListDrawHandlers(ListView list)
        {
            list.BeginUpdate();
            try
            {
                list.DrawColumnHeader -= DrawListHeader; list.DrawItem -= DrawListItem; list.DrawSubItem -= DrawListSubItem;
                if (list.OwnerDraw) list.OwnerDraw = false;
                if (listGridLinesChanged && !list.GridLines) list.GridLines = listOriginalGridLines;
                listGridLinesChanged = false;
                ownsListDraw = false;
                RestoreListBuffering(list);
            }
            finally { list.EndUpdate(); }
        }
        private void DrawTree(object sender, DrawTreeNodeEventArgs e)
        {
            bool selected = control.Enabled && (e.State & TreeNodeStates.Selected) != 0;
            bool focused = ((TreeView)sender).Focused;
            Color back = selected ? current.Colors[focused ? 11 : 13] : (!e.Node.BackColor.IsEmpty ? e.Node.BackColor : current.Colors[2]);
            Color fore = selected ? current.Colors[focused || current.Kind == 2 ? 12 : 4] : (!e.Node.ForeColor.IsEmpty ? e.Node.ForeColor : current.Colors[4]);
            if (!control.Enabled) fore = current.Colors[7];
            using (Brush brush = new SolidBrush(back)) e.Graphics.FillRectangle(brush, e.Bounds);
            TextRenderer.DrawText(e.Graphics, e.Node.Text, e.Node.NodeFont ?? control.Font, e.Bounds, fore, TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
            if (selected && focused) ControlPaint.DrawFocusRectangle(e.Graphics, e.Bounds, fore, back);
        }
        private static void DrawListHeader(object sender, DrawListViewColumnHeaderEventArgs e) { e.DrawDefault = true; }
        private static void DrawListItem(object sender, DrawListViewItemEventArgs e) { e.DrawDefault = false; }
        private void DrawListSubItem(object sender, DrawListViewSubItemEventArgs e)
        {
            ListView list = (ListView)sender;
            bool selected = list.Enabled && e.Item.Selected, focused = list.Focused;
            Color back = selected ? current.Colors[focused ? 11 : 13] : (IsOrdinary(e.SubItem.BackColor) ? current.Colors[2] : e.SubItem.BackColor);
            Color fore = !list.Enabled ? current.Colors[7] : selected ? current.Colors[focused || current.Kind == 2 ? 12 : 4] : (IsOrdinary(e.SubItem.ForeColor) ? current.Colors[4] : e.SubItem.ForeColor);
            using (Brush brush = new SolidBrush(back)) e.Graphics.FillRectangle(brush, e.Bounds);
            TextFormatFlags flags = TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis;
            if (e.Header.TextAlign == HorizontalAlignment.Right) flags |= TextFormatFlags.Right;
            else if (e.Header.TextAlign == HorizontalAlignment.Center) flags |= TextFormatFlags.HorizontalCenter;
            TextRenderer.DrawText(e.Graphics, e.SubItem.Text, e.SubItem.Font ?? list.Font, e.Bounds, fore, flags);
            if (selected && focused && e.ColumnIndex == 0) ControlPaint.DrawFocusRectangle(e.Graphics, e.Bounds, fore, back);
            if (listGridLinesChanged && !list.GridLines && e.Bounds.Width > 0 && e.Bounds.Height > 0)
            {
                using (Pen pen = new Pen(current.Colors[8]))
                {
                    int right = e.Bounds.Right - 1, bottom = e.Bounds.Bottom - 1;
                    e.Graphics.DrawLine(pen, e.Bounds.Left, bottom, right, bottom);
                    e.Graphics.DrawLine(pen, right, e.Bounds.Top, right, bottom);
                }
            }
        }
        private void DrawCombo(object sender, DrawItemEventArgs e)
        {
            ComboBox combo = (ComboBox)sender;
            bool selected = combo.Enabled && (e.State & DrawItemState.Disabled) == 0 &&
                (e.State & DrawItemState.Selected) != 0;
            Color back = selected ? current.Colors[11] : current.Colors[2];
            Color fore = !combo.Enabled || (e.State & DrawItemState.Disabled) != 0 ? current.Colors[7] : selected ? current.Colors[12] : current.Colors[4];
            using (Brush brush = new SolidBrush(back)) e.Graphics.FillRectangle(brush, e.Bounds);
            if (e.Index >= 0 && e.Index < combo.Items.Count) TextRenderer.DrawText(e.Graphics, combo.GetItemText(combo.Items[e.Index]), combo.Font, e.Bounds, fore, TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            if ((e.State & DrawItemState.Focus) != 0) ControlPaint.DrawFocusRectangle(e.Graphics, e.Bounds, fore, back);
        }
        private void DrawTab(object sender, DrawItemEventArgs e)
        {
            TabControl tabs = (TabControl)sender;
            if (e.Index < 0 || e.Index >= tabs.TabPages.Count) return;
            bool selected = tabs.Enabled && tabs.TabPages[e.Index].Enabled && e.Index == tabs.SelectedIndex;
            Color back = current.Colors[selected ? 11 : 1], fore = current.Colors[!tabs.Enabled || !tabs.TabPages[e.Index].Enabled ? 7 : selected ? 12 : 4];
            using (Brush brush = new SolidBrush(back)) e.Graphics.FillRectangle(brush, e.Bounds);
            TextRenderer.DrawText(e.Graphics, tabs.TabPages[e.Index].Text, tabs.Font, e.Bounds, fore, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            if (selected && tabs.Focused) ControlPaint.DrawFocusRectangle(e.Graphics, e.Bounds, fore, back);
        }
        private static Color[] GetGridColors(PropertyGrid p) { return new[] { p.ViewBackColor, p.ViewForeColor, p.HelpBackColor, p.HelpForeColor, p.CategoryForeColor, p.CommandsForeColor, p.CommandsBackColor, p.LineColor, p.CommandsLinkColor, p.CommandsActiveLinkColor, p.CommandsDisabledLinkColor }; }
        private static void SetGridColor(PropertyGrid p, int index, Color c)
        {
            switch (index) {
                case 0: p.ViewBackColor=c; break; case 1: p.ViewForeColor=c; break; case 2: p.HelpBackColor=c; break; case 3: p.HelpForeColor=c; break;
                case 4: p.CategoryForeColor=c; break; case 5: p.CommandsForeColor=c; break; case 6: p.CommandsBackColor=c; break; case 7: p.LineColor=c; break;
                case 8: p.CommandsLinkColor=c; break; case 9: p.CommandsActiveLinkColor=c; break; case 10: p.CommandsDisabledLinkColor=c; break;
            }
        }
    }

    internal sealed class ThemeStripRenderer : ToolStripProfessionalRenderer
    {
        private readonly ThemeSnapshot theme;
        internal ThemeStripRenderer(ThemeSnapshot snapshot) : base(new ThemeStripColors(snapshot)) { theme = snapshot; RoundedEdges = false; }
        protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
        {
            ToolStripButton button = e.Item as ToolStripButton;
            bool selected = e.Item.Pressed || (e.Item is ToolStripMenuItem && e.Item.Selected) ||
                (button != null && button.CheckState != CheckState.Unchecked);
            if (!e.Item.Enabled || selected || e.Item.Selected || WinFormsThemeAdapter.IsOrdinary(e.TextColor) ||
                e.TextColor == theme.Colors[4] || e.TextColor == theme.Colors[5] ||
                e.TextColor == theme.Colors[6] || e.TextColor == theme.Colors[7])
                e.TextColor = !e.Item.Enabled ? theme.Colors[7] : selected ? theme.Colors[12] :
                    e.Item.Selected ? theme.Colors[5] : theme.Colors[4];
            base.OnRenderItemText(e);
        }
        protected override void OnRenderItemCheck(ToolStripItemImageRenderEventArgs e)
        {
            ToolStripMenuItem item = e.Item as ToolStripMenuItem;
            if (item == null || item.GetType() != typeof(ToolStripMenuItem) ||
                item.CheckState == CheckState.Unchecked || e.ImageRectangle.Width <= 0 || e.ImageRectangle.Height <= 0)
            { base.OnRenderItemCheck(e); return; }
            Color back = theme.Colors[item.Enabled && item.Selected ? 11 : 1];
            Color fore = theme.Colors[!item.Enabled ? 7 : item.Selected ? 12 : 4];
            using (Brush brush = new SolidBrush(back)) e.Graphics.FillRectangle(brush, e.ImageRectangle);
            if (item.CheckState == CheckState.Indeterminate)
            {
                Rectangle mark = e.ImageRectangle;
                mark.Inflate(-Math.Max(1, mark.Width / 4), -Math.Max(1, mark.Height / 3));
                using (Brush brush = new SolidBrush(fore)) e.Graphics.FillRectangle(brush, mark);
            }
            else ControlPaint.DrawMenuGlyph(e.Graphics, e.ImageRectangle, MenuGlyph.Checkmark, fore, back);
        }
        protected override void OnRenderButtonBackground(ToolStripItemRenderEventArgs e)
        {
            if (e.Item.Enabled || e.Item.GetType() != typeof(ToolStripButton))
            { base.OnRenderButtonBackground(e); return; }
            Rectangle bounds = new Rectangle(Point.Empty, e.Item.Size);
            using (Brush brush = new SolidBrush(theme.Colors[1])) e.Graphics.FillRectangle(brush, bounds);
            ToolStripButton button = (ToolStripButton)e.Item;
            if (button.CheckState != CheckState.Unchecked && bounds.Width > 1 && bounds.Height > 1)
            {
                bounds.Width -= 1; bounds.Height -= 1;
                using (Pen pen = new Pen(theme.Colors[7])) e.Graphics.DrawRectangle(pen, bounds);
            }
        }
        protected override void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs e)
        {
            if (e.Item.Enabled || e.Item.GetType() != typeof(ToolStripMenuItem))
            { base.OnRenderMenuItemBackground(e); return; }
            using (Brush brush = new SolidBrush(theme.Colors[1]))
                e.Graphics.FillRectangle(brush, new Rectangle(Point.Empty, e.Item.Size));
        }
    }
    internal sealed class ThemeStripColors : ProfessionalColorTable
    {
        private readonly ThemeSnapshot t;
        internal ThemeStripColors(ThemeSnapshot theme) { t = theme; UseSystemColors = false; }
        public override Color ToolStripGradientBegin { get { return t.Colors[1]; } }
        public override Color ToolStripGradientMiddle { get { return t.Colors[1]; } }
        public override Color ToolStripGradientEnd { get { return t.Colors[1]; } }
        public override Color ToolStripBorder { get { return t.Colors[8]; } }
        public override Color ToolStripContentPanelGradientBegin { get { return t.Colors[1]; } }
        public override Color ToolStripContentPanelGradientEnd { get { return t.Colors[1]; } }
        public override Color ToolStripPanelGradientBegin { get { return t.Colors[1]; } }
        public override Color ToolStripPanelGradientEnd { get { return t.Colors[1]; } }
        public override Color StatusStripGradientBegin { get { return t.Colors[1]; } }
        public override Color StatusStripGradientEnd { get { return t.Colors[1]; } }
        public override Color RaftingContainerGradientBegin { get { return t.Colors[1]; } }
        public override Color RaftingContainerGradientEnd { get { return t.Colors[1]; } }
        public override Color OverflowButtonGradientBegin { get { return t.Colors[1]; } }
        public override Color OverflowButtonGradientMiddle { get { return t.Colors[1]; } }
        public override Color OverflowButtonGradientEnd { get { return t.Colors[1]; } }
        public override Color GripDark { get { return t.Colors[6]; } }
        public override Color GripLight { get { return t.Colors[1]; } }
        public override Color MenuStripGradientBegin { get { return t.Colors[1]; } }
        public override Color MenuStripGradientEnd { get { return t.Colors[1]; } }
        public override Color ToolStripDropDownBackground { get { return t.Colors[1]; } }
        public override Color ImageMarginGradientBegin { get { return t.Colors[1]; } }
        public override Color ImageMarginGradientMiddle { get { return t.Colors[1]; } }
        public override Color ImageMarginGradientEnd { get { return t.Colors[1]; } }
        public override Color ImageMarginRevealedGradientBegin { get { return t.Colors[1]; } }
        public override Color ImageMarginRevealedGradientMiddle { get { return t.Colors[1]; } }
        public override Color ImageMarginRevealedGradientEnd { get { return t.Colors[1]; } }
        public override Color MenuBorder { get { return t.Colors[8]; } }
        public override Color MenuItemSelected { get { return t.Colors[11]; } }
        public override Color MenuItemSelectedGradientBegin { get { return t.Colors[11]; } }
        public override Color MenuItemSelectedGradientEnd { get { return t.Colors[11]; } }
        public override Color MenuItemPressedGradientBegin { get { return t.Colors[11]; } }
        public override Color MenuItemPressedGradientMiddle { get { return t.Colors[11]; } }
        public override Color MenuItemPressedGradientEnd { get { return t.Colors[11]; } }
        public override Color MenuItemBorder { get { return t.Colors[10]; } }
        public override Color ButtonSelectedGradientBegin { get { return t.Colors[9]; } }
        public override Color ButtonSelectedGradientMiddle { get { return t.Colors[9]; } }
        public override Color ButtonSelectedGradientEnd { get { return t.Colors[9]; } }
        public override Color ButtonSelectedHighlight { get { return t.Colors[9]; } }
        public override Color ButtonSelectedHighlightBorder { get { return t.Colors[10]; } }
        public override Color ButtonSelectedBorder { get { return t.Colors[10]; } }
        public override Color ButtonPressedGradientBegin { get { return t.Colors[11]; } }
        public override Color ButtonPressedGradientMiddle { get { return t.Colors[11]; } }
        public override Color ButtonPressedGradientEnd { get { return t.Colors[11]; } }
        public override Color ButtonPressedHighlight { get { return t.Colors[11]; } }
        public override Color ButtonPressedHighlightBorder { get { return t.Colors[10]; } }
        public override Color ButtonPressedBorder { get { return t.Colors[10]; } }
        public override Color ButtonCheckedGradientBegin { get { return t.Colors[11]; } }
        public override Color ButtonCheckedGradientMiddle { get { return t.Colors[11]; } }
        public override Color ButtonCheckedGradientEnd { get { return t.Colors[11]; } }
        public override Color ButtonCheckedHighlight { get { return t.Colors[11]; } }
        public override Color ButtonCheckedHighlightBorder { get { return t.Colors[10]; } }
        public override Color CheckBackground { get { return t.Colors[1]; } }
        public override Color CheckSelectedBackground { get { return t.Colors[11]; } }
        public override Color CheckPressedBackground { get { return t.Colors[11]; } }
        public override Color SeparatorDark { get { return t.Colors[8]; } }
        public override Color SeparatorLight { get { return t.Colors[8]; } }
    }
}
