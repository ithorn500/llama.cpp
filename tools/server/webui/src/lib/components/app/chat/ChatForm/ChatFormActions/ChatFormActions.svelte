<script lang="ts">
	import { Square } from '@lucide/svelte';
	import { Button } from '$lib/components/ui/button';
	import {
		ChatFormActionsAdd,
		ChatFormActionModels,
		ChatFormActionRecord,
		ChatFormActionSubmit
	} from '$lib/components/app';
	import { FileTypeCategory } from '$lib/enums';
	import { mcpStore } from '$lib/stores/mcp.svelte';
	import { config } from '$lib/stores/settings.svelte';
	import { conversationsStore } from '$lib/stores/conversations.svelte';
	import { getFileTypeCategory } from '$lib/utils';
	import { goto } from '$app/navigation';
	import { ROUTES } from '$lib/constants/routes';

	interface Props {
		canSend?: boolean;
		canSubmit?: boolean;
		class?: string;
		disabled?: boolean;
		isLoading?: boolean;
		isRecording?: boolean;
		showAddButton?: boolean;
		showModelSelector?: boolean;
		memoryNamespace?: string;
		memoryProfile?: string;
		memoryProfiles?: string[];
		memorySpace?: string;
		memorySpaces?: string[];
		hasText?: boolean;
		uploadedFiles?: ChatUploadedFile[];
		onFileUpload?: () => void;
		onMemoryProfileAdd?: (profile: string) => void;
		onMemorySpaceAdd?: (space: string) => void;
		onMicClick?: () => void;
		onStop?: () => void;
		onSystemPromptClick?: () => void;
		onMcpPromptClick?: () => void;
		onMcpResourcesClick?: () => void;
	}

	let {
		canSend = false,
		canSubmit = false,
		class: className = '',
		disabled = false,
		isLoading = false,
		isRecording = false,
		showAddButton = true,
		showModelSelector = true,
		memoryNamespace = '',
		memoryProfile = $bindable('Iain'),
		memoryProfiles = [],
		memorySpace = $bindable('personal'),
		memorySpaces = [],
		hasText = false,
		uploadedFiles = [],
		onFileUpload,
		onMemoryProfileAdd,
		onMemorySpaceAdd,
		onMicClick,
		onStop,
		onSystemPromptClick,
		onMcpPromptClick,
		onMcpResourcesClick
	}: Props = $props();

	let currentConfig = $derived(config());

	let hasMcpPromptsSupport = $derived.by(() => {
		const perChatOverrides = conversationsStore.getAllMcpServerOverrides();

		return mcpStore.hasPromptsCapability(perChatOverrides);
	});

	let hasMcpResourcesSupport = $derived.by(() => {
		const perChatOverrides = conversationsStore.getAllMcpServerOverrides();

		return mcpStore.hasResourcesCapability(perChatOverrides);
	});

	let hasAudioModality = $state(false);
	let hasVisionModality = $state(false);
	let hasModelSelected = $state(false);
	let isSelectedModelInCache = $state(true);
	let submitTooltip = $state('');

	let hasAudioAttachments = $derived(
		uploadedFiles.some((file) => getFileTypeCategory(file.type) === FileTypeCategory.AUDIO)
	);
	let shouldShowRecordButton = $derived(
		hasAudioModality && !canSubmit && !hasAudioAttachments && currentConfig.autoMicOnEmpty
	);

	let selectorModelRef: ChatFormActionModels | undefined = $state(undefined);

	export function openModelSelector() {
		selectorModelRef?.open();
	}

	function handleProfileSelect(event: Event) {
		const value = (event.currentTarget as HTMLSelectElement).value;
		if (value !== '__add_new__') {
			memoryProfile = value;
			return;
		}
		const next = window.prompt('Add profile');
		if (next?.trim()) {
			onMemoryProfileAdd?.(next);
		} else {
			memoryProfile = memoryProfiles[0] || 'Iain';
		}
	}

	function handleSpaceSelect(event: Event) {
		const value = (event.currentTarget as HTMLSelectElement).value;
		if (value !== '__add_new__') {
			memorySpace = value;
			return;
		}
		const next = window.prompt('Add space');
		if (next?.trim()) {
			onMemorySpaceAdd?.(next);
		} else {
			memorySpace = memorySpaces[0] || 'personal';
		}
	}
</script>

<div
	class="flex w-full items-center gap-3 {className} {showAddButton ? '' : 'justify-end'}"
	style="container-type: inline-size"
>
	{#if showAddButton}
		<div class="mr-auto flex items-center gap-2">
			<ChatFormActionsAdd
				{disabled}
				{hasAudioModality}
				{hasVisionModality}
				{hasMcpPromptsSupport}
				{hasMcpResourcesSupport}
				{onFileUpload}
				{onSystemPromptClick}
				{onMcpPromptClick}
				{onMcpResourcesClick}
				onMcpSettingsClick={() => goto(ROUTES.MCP_SERVERS)}
			/>
		</div>
	{/if}

	<div class="ml-auto flex items-center gap-2">
		<div
			class="hidden min-w-0 items-center gap-1 rounded-lg bg-muted/45 px-2 py-1 shadow-sm ring-1 ring-border/60 sm:flex"
			title={memoryNamespace}
		>
			<span class="text-[10px] font-medium uppercase tracking-normal text-muted-foreground">mem</span>
			<select
				bind:value={memoryProfile}
				onchange={handleProfileSelect}
				aria-label="Memory profile"
				class="h-6 w-24 rounded-md border border-transparent bg-background/80 px-2 text-xs text-foreground outline-none focus:border-ring"
			>
				{#each memoryProfiles as profile}
					<option value={profile}>{profile}</option>
				{/each}
				<option value="__add_new__">Add new...</option>
			</select>
			<span class="text-xs text-muted-foreground">/</span>
			<select
				bind:value={memorySpace}
				onchange={handleSpaceSelect}
				aria-label="Userspace"
				class="h-6 w-24 rounded-md border border-transparent bg-background/80 px-2 text-xs text-foreground outline-none focus:border-ring"
			>
				{#each memorySpaces as space}
					<option value={space}>{space}</option>
				{/each}
				<option value="__add_new__">Add new...</option>
			</select>
		</div>
		{#if showModelSelector}
			<ChatFormActionModels
				{disabled}
				bind:this={selectorModelRef}
				bind:hasAudioModality
				bind:hasVisionModality
				bind:hasModelSelected
				bind:isSelectedModelInCache
				bind:submitTooltip
				forceForegroundText
				useGlobalSelection
			/>
		{/if}
	</div>

	{#if isLoading && !canSubmit}
		<Button
			type="button"
			variant="secondary"
			onclick={onStop}
			class="group h-8 w-8 rounded-full p-0 hover:bg-destructive/10!"
		>
			<span class="sr-only">Stop</span>

			<Square
				class="h-8 w-8 fill-muted-foreground stroke-muted-foreground group-hover:fill-destructive group-hover:stroke-destructive hover:fill-destructive hover:stroke-destructive"
			/>
		</Button>
	{:else if shouldShowRecordButton}
		<ChatFormActionRecord {disabled} {hasAudioModality} {isLoading} {isRecording} {onMicClick} />
	{:else}
		<ChatFormActionSubmit
			canSend={canSend && (showModelSelector ? hasModelSelected && isSelectedModelInCache : true)}
			{disabled}
			tooltipLabel={submitTooltip}
			showErrorState={showModelSelector && hasModelSelected && !isSelectedModelInCache}
		/>
	{/if}
</div>
